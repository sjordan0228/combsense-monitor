# CombSense Web Dashboard — Design Spec

**Date:** 2026-04-20
**Scope:** Browser-based admin dashboard for CombSense. Drill-down `user → yards → hives → devices` over Influx time-series, plus full admin capabilities (users, devices, alerts, OTA, audit). LAN-only day one; built to go public without a rewrite.

---

## 1. Goals / Non-goals

### Goals
- Hierarchical browsing: `user → yards → hives → devices → time-series charts`
- Full admin console: user management, device provisioning (auto-claim + assign), alerts, audit log, token management
- LAN-first deployment, designed so flipping to public-internet is a deployment change, not a rewrite
- Separation from TSDB LXC — independent blast radius
- Reuses existing Influx data path unchanged; no firmware changes for v1

### Non-goals (v1)
- Beekeeping metadata (inspections, queen info, treatments, photos, hive cost) — stays on the iOS app / SwiftData
- Real-time streaming updates (websocket/SSE) — v1 uses HTMX polling
- OTA rollouts — deferred to Phase 2
- Humidity / SHT31 charts — deferred to Phase 3 when SHT31 hardware is proven (current defective Teyleten boards await Adafruit replacements)
- IR bee counter views — depends on Phase 2 firmware
- MFA — `django-otp` drop-in when needed, not v1
- Multi-tenant productionization — auth model is multi-tenant-ready; public deployment is Phase 2

---

## 2. Architecture

### Two-LXC split

| LXC | Role |
|-----|------|
| `combsense-tsdb` (existing, 192.168.1.19) | Mosquitto, Telegraf, InfluxDB 2.8, Grafana — **unchanged** |
| `combsense-web` (new) | nginx, Django (gunicorn), Celery, Redis, Postgres 16, MQTT ingest subscriber |

Grafana stays available for ad-hoc queries. Nothing in the TSDB LXC changes.

### Services on `combsense-web`

| Service | Purpose |
|---------|---------|
| nginx | TLS termination, static assets, proxy to gunicorn. Self-signed cert on LAN day one; Let's Encrypt in Phase 2 |
| Django (gunicorn) | HTTP app: views, HTMX endpoints, Django admin for low-level CRUD |
| Celery worker | Background jobs: alert rule evaluation, OTA dispatch (Phase 2) |
| Celery beat | Periodic scheduler for alert polling |
| Redis | Celery broker + Django cache |
| Postgres 16 | Metadata (users, yards, hives, devices, alerts, audit) |
| MQTT ingest subscriber | Listens to `combsense/hive/+/reading`. Auto-creates `Device` rows for unknown `sensor_id`s; updates `last_seen` on known devices. Runs as its own systemd service. |

### Two ingress paths

Both paths land the same MQTT payload at the broker; the dashboard doesn't distinguish at the data layer:

- **LAN / home-yard:** `sensor-tag-wifi` (XIAO ESP32-C6, `firmware/sensor-tag-wifi/`) → WiFi → Mosquitto directly. Current home deployment.
- **Remote / apiary:** `hive-node` (ESP32-S3, `firmware/hive-node/`) → ESP-NOW → `collector` (LilyGO T-SIM7080G, `firmware/collector/`) → cellular → Mosquitto. Awaits apiary hardware.

`Device.kind` carries the distinction for display and ops (e.g. "collector offline" only applies to remote yards).

### Data flow

1. **Charts:** Browser → Django → Postgres (resolve `sensor_id`s for a hive) → Influx read (Flux, `ios_read_token`) → Chart.js render. Auto-bucket logic mirrors `HistoryService` in the iOS app: short ranges hit `combsense` (raw, 30d), medium ranges hit `combsense_1h` (hourly, 365d), long ranges hit `combsense_1d` (daily, ∞). Exact cutoffs to match the iOS implementation during planning.
2. **Device auto-claim:** MQTT subscriber sees a reading with unknown `sensor_id` → inserts `Device` with `hive_id=NULL`, `claimed_at=NULL` → device appears in "Unclaimed" list in UI. Admin clicks "Assign to hive"; no device-side interaction.
3. **Alerts:** Celery beat fires periodic task per `AlertRule.is_enabled=true`. Task runs Flux query for the last N minutes, evaluates threshold, enqueues email via SMTP on fire, writes `AuditEvent`.
4. **OTA (Phase 2):** Admin uploads firmware → Django stores binary + sha256 → publishes MQTT command to collector `combsense/collector/+/cmd`. Collector firmware handles ESP-NOW chunking (existing capability). Rollout status tracked via status topics.

---

## 3. Data Model

Nine Postgres tables. Time-series stays in Influx, joined by `sensor_id`.

```
User (id, email, display_name, password_hash, role{admin|beekeeper}, is_active, date_joined, last_login)
Yard (id, owner_id→User, name, latitude, longitude, timezone, notes, created_at)
Hive (id, yard_id→Yard, name, install_date, is_active, notes, created_at)
Device (id, hive_id→Hive nullable, mac_addr unique, sensor_id unique,
        kind{sensor-tag-wifi|hive-node|collector}, firmware_version,
        last_seen, mqtt_username, claimed_at)
AlertRule (id, hive_id→Hive, metric{t1|b}, op{gt|lt},
           threshold, channel{email},
           notify_address, is_enabled, last_fired_at nullable, created_at)
FirmwareBuild (id, kind, version, url, sha256, notes, uploaded_at)    -- Phase 2
OtaRollout (id, build_id→FirmwareBuild, target_type, target_id,
            status, started_at, finished_at)                           -- Phase 2
AuditEvent (id, actor_id→User nullable, action, target_type, target_id,
            payload jsonb, created_at)
ApiToken (id, user_id→User, label, token_hash, scope, last_used, revoked_at)
```

**v1 charting note:** `t1` is temperature; `t2` is humidity (hidden for DS18B20-only hives). `b` is battery. When SHT31 hardware is ready, the humidity column already exists — just unhide in the chart.

**Capability modeling:** v1 does **not** model per-device sensor capabilities. The chart renders whichever fields are present for a given `sensor_id` in Influx. When a hive has only DS18B20 devices, the humidity series is omitted because the field is always null in those readings.

---

## 4. UI Structure

### Navigation

Sidebar-plus-breadcrumbs hybrid:

- **Left sidebar (always visible):**
  - Sections: Users, Devices, Firmware · OTA (Phase 2), Alerts, Audit log
  - Pinned hives (user-specific, for quick access)
- **Main area:** breadcrumb trail (`shane › Home yard › Hive #1`) and tabs on detail pages

### Pages (v1)

| Page | Contents |
|------|----------|
| Users list | Email, role, last login, hive count, active toggle. Admin-only |
| User detail | Yards owned, pinned hives, recent activity. Admin-only |
| Yard list (per user) | Name, hive count, last reading, location |
| Yard detail | Hives list with status + latest temp/battery + last-seen |
| **Hive detail** | Four tabs — see below |
| Devices list | All devices, filter by kind / claimed / online. Bulk-claim UI |
| Device detail | Firmware version, last seen, raw recent readings, assignment history |
| Alerts list | All rules across hives, filter by hive/status. Recent firings |
| Audit log | Paginated, filter by actor / action / target |
| Account settings | Change password, email, pinned hives, API tokens |

### Hive detail page — four tabs

1. **Readings (default):**
   - KPI strip: current temp, battery, last reading age, alerts fired (24h)
   - Temperature chart with range picker (1h / 24h / 7d / 30d / 1y)
   - Battery chart with same range picker
   - Range selection drives bucket resolution (raw / 1h / 1d) via the same logic as the iOS HistoryService
2. **Devices:** devices in this hive, status (online/offline), firmware, sensor kind. Assign/reassign/unassign actions.
3. **Alerts:** alert rules for this hive, create/edit/delete, recent firings table.
4. **Settings:** rename, move hive to another yard, mark active/archived, notes (dashboard-side notes only — beekeeping notes live in iOS).

### Charts

- Chart.js via CDN
- HTMX polling for KPI strip refresh every 30s
- Range picker re-fetches via HTMX partial swap; new Flux query per range

---

## 5. Ingest + Device Claim Flow

The MQTT ingest subscriber is the only new service talking to Mosquitto.

**Algorithm:**
```
on message from combsense/hive/<sensor_id>/reading:
    device = Postgres.select Device where sensor_id=<sensor_id>
    if device is None:
        Postgres.insert Device(
            sensor_id=<sensor_id>, mac_addr=<from payload or null>,
            kind=<guessed from payload fields>, hive_id=NULL,
            claimed_at=NULL, last_seen=now())
        AuditEvent(action='device.autocreated', target=device.id)
    else:
        Postgres.update Device set last_seen=now(),
                                   firmware_version=<payload.fw if present>
```

**Observations:**
- This subscriber is read-only against Influx; Telegraf remains the writer. No contention.
- `kind` guess: look for payload fields. `weight` present → `hive-node`. Wrapped by collector payload → `hive-node` with collector attribution. Else → best-effort `sensor-tag-wifi`. Can be corrected manually in Device detail.
- If the subscriber is down, no data is lost — it's purely for metadata; Influx ingestion via Telegraf continues. On restart, `last_seen` catches up.

---

## 6. Alerts

- Rules are per-hive, per-metric (`t1` or `b` in v1), single-threshold operator (`gt`, `lt`)
- Evaluation: Celery beat schedules one periodic task that iterates active rules. Task queries Influx for the last 10 minutes per rule, checks threshold, fires on crossing
- Debounce: 1 hour minimum between re-fires for the same rule (`AlertRule.last_fired_at`)
- Channel v1: email (SMTP). Webhook and `outside` range operator deferred to Phase 2
- Firings recorded as `AuditEvent` rows for history

---

## 7. Auth & Permissions

- Django's built-in `auth.User` (custom model subclassing `AbstractUser`, with `role` and `is_active`). Email is the login identifier.
- Session cookies, CSRF protection, standard Django middleware
- 14-day session idle timeout; "remember me" via extended-lifetime signed cookie
- Invite-only: admin creates User in admin UI, user receives one-time password-reset link via email. No open signup
- Two roles:
  - `admin`: full access to all resources
  - `beekeeper`: scoped via object-level permissions middleware — can see only yards/hives where `Yard.owner_id == request.user.id`
- MFA deferred (`django-otp` when needed)
- API tokens stored as hash; scope is a simple string (`ios_read`, `provisioning`, etc.)

---

## 8. Deployment

### LXC: `combsense-web`

- Proxmox LXC, unprivileged Debian 12, NFS-backed (same pattern as `combsense-tsdb`)
- Systemd drop-ins for Grafana-style sandboxing workarounds — see memory on `Unprivileged LXC Sandboxing`
- IP: TBD (next in 192.168.1.x range, allocated before build)

### Services (all systemd)

| Service | Notes |
|---------|-------|
| `postgresql@16-main` | Local only, 127.0.0.1:5432 |
| `redis-server` | Local only |
| `combsense-web.service` | gunicorn → Django, bound to 127.0.0.1:8000 |
| `combsense-celery.service` | Celery worker |
| `combsense-celery-beat.service` | Celery beat |
| `combsense-ingest.service` | MQTT subscriber, paho-mqtt client, reconnect loop |
| `nginx` | Listens :443 (self-signed), :80 redirects |

### Secrets

- `.env` at `/etc/combsense-web/env` (mode 600, owned by app user). Contains: `DJANGO_SECRET_KEY`, `POSTGRES_DSN`, `REDIS_URL`, `MQTT_URL`, `MQTT_USER`, `MQTT_PASS`, `INFLUX_URL`, `INFLUX_TOKEN`, `INFLUX_ORG`, `SMTP_*`
- Influx read token reused from `/root/.combsense-tsdb-creds` on the TSDB box — copied, not shared
- Separate MQTT user `combsense-web` with subscribe-only ACL on `combsense/#`

### Backups

- Daily `pg_dump` via systemd timer, 14-day retention (mirrors TSDB pattern)
- Redis is cache/broker only — not backed up
- Firmware blobs (Phase 2) stored on disk under `/var/lib/combsense-web/firmware/`; included in backup

### Network

- Day 1: LAN only. nginx binds `192.168.1.x:443`. Self-signed cert. Access via hostname in local DNS.
- Phase 2: domain, DNS, Let's Encrypt, fail2ban, rate limits on login. No source-code changes to enable — deployment config only.

---

## 9. Phasing

### Phase 1 (v1) — this spec drives implementation
- LXC build + service install
- Django project, custom User, Postgres schema migrations
- Auth (login, logout, password reset, invite flow skeleton — email delivery optional, manual token link acceptable in v1)
- MQTT ingest subscriber + auto-claim
- CRUD: Yards, Hives, Device assignment
- Hive detail page with four tabs
- Devices list with filter + claim UI
- Alert rules + SMTP email delivery + Celery evaluation
- Audit log view
- Chart.js integration with range-based bucket resolution
- nginx + self-signed TLS + LAN access

### Phase 2
- Firmware upload + sha256 verification
- OTA rollout UI + MQTT command publisher
- API token management UI (v1 uses Django admin)
- Public-internet deployment: domain, Let's Encrypt, rate limits, fail2ban
- Webhook alert channel
- Per-user permission hardening + beekeeper role full integration

### Phase 3
- SHT31 humidity charts (when hardware proven)
- IR bee-counter views (requires Phase 2 firmware work)
- `django-otp` MFA
- Invite email polish (branded templates, SMTP sender reputation)
- Grafana SSO integration (optional)

---

## 10. Integration Points (existing systems)

| System | Relationship |
|--------|-------------|
| Mosquitto broker (192.168.1.82:1883) | New subscriber `combsense/hive/+/reading` (read-only); Phase 2 adds publisher on `combsense/collector/+/cmd` |
| InfluxDB | Read via existing `ios_read_token`; no schema changes |
| Grafana | Unchanged; stays available for ad-hoc |
| iOS app | Unchanged; shares the same Influx data and read token |
| `tools/provision_tag.py` | Unchanged in v1; Phase 2 may add credential minting UI that mirrors its token flow |

---

## 11. Open Questions (to resolve during planning, not blocking spec approval)

- Exact LXC IP + hostname
- Postgres sizing (start: 4GB RAM, 20GB disk)
- SMTP relay choice for outbound (local sendmail vs external provider — Mailgun/SES free tier)
- Self-signed cert generation method (mkcert for LAN-trusted dev, or just ignore-cert-warnings for v1)
- Whether the ingest subscriber also updates an "online/offline" derived state or if the dashboard computes it on the fly from `last_seen`
