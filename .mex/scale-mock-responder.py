#!/usr/bin/env python3
"""
Mock firmware responder for the CombSense scale/cmd + scale/config contract.
Subscribes to combsense/hive/+/scale/cmd AND combsense/hive/+/scale/config,
parses, and publishes matching scale/status events. Lets us exercise the
iOS round-trip without real hardware.

scale/config behavior (addendum 1.1):
- A retained payload with future keep_alive_until puts the hive into
  "extended-awake mode" — we publish an `awake` event immediately and
  heartbeat every 60s.
- An empty retained payload clears the awake mode.
- The wake-delay before the first awake event is configurable via the
  WAKE_DELAY_SEC env var (default 3s) to simulate the firmware taking
  some time to wake from deep sleep.
"""
import json
import os
import subprocess
import sys
import time
import threading
import time as _time
import math
import random
from datetime import datetime, timezone, timedelta

BROKER = "192.168.1.82"
PORT = "1883"
USER = "hivesense"
PASS = "hivesense"

WAKE_DELAY_SEC = float(os.environ.get("WAKE_DELAY_SEC", "3"))
HEARTBEAT_INTERVAL_SEC = float(os.environ.get("HEARTBEAT_INTERVAL_SEC", "60"))

def now_iso():
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

def parse_iso(s):
    # Accept both fractional and non-fractional ISO8601 with Z.
    s = s.rstrip("Z")
    fmt = "%Y-%m-%dT%H:%M:%S.%f" if "." in s else "%Y-%m-%dT%H:%M:%S"
    return datetime.strptime(s, fmt).replace(tzinfo=timezone.utc)

_streamers = {}        # hive_id -> threading.Event (set to stop)
_awake = {}            # hive_id -> {"stop": Event, "keep_alive_until": str}

def _publish(topic, payload, retained=False):
    args = [
        "mosquitto_pub",
        "-h", BROKER, "-p", PORT,
        "-u", USER, "-P", PASS,
        "-t", topic, "-m", payload,
    ]
    if retained:
        args.append("-r")
    subprocess.run(args, check=False)

def _start_stream(hive_id, duration_sec):
    """Stream fake rawStream events at 1Hz for `duration_sec` seconds."""
    stop = _streamers.get(hive_id)
    if stop:
        stop.set()
    new_stop = threading.Event()
    _streamers[hive_id] = new_stop

    def loop():
        start = _time.time()
        i = 0
        while not new_stop.is_set() and (_time.time() - start) < duration_sec:
            elapsed = _time.time() - start
            base = 5_678_900
            jitter = int(random.gauss(0, 50)) if elapsed < 5 else int(random.gauss(0, 5))
            raw = base + jitter
            kg = raw / 120_000.0
            stable = elapsed > 5
            event = {
                "event": "raw_stream",
                "raw_value": raw,
                "kg": round(kg, 3),
                "stable": stable,
                "ts": now_iso(),
            }
            _publish(f"combsense/hive/{hive_id}/scale/status", json.dumps(event))
            i += 1
            _time.sleep(1.0)
        _streamers.pop(hive_id, None)

    threading.Thread(target=loop, daemon=True).start()

def _stop_stream(hive_id):
    stop = _streamers.get(hive_id)
    if stop:
        stop.set()

def _enter_awake(hive_id, keep_alive_until_str):
    """Enter extended-awake mode for hive. Publishes initial `awake` event
    after WAKE_DELAY_SEC, then heartbeats every HEARTBEAT_INTERVAL_SEC
    until cleared or keep_alive_until passes."""
    existing = _awake.get(hive_id)
    if existing:
        # Refresh: just update the keep_alive_until on the existing thread.
        existing["keep_alive_until"] = keep_alive_until_str
        print(f"[responder] {hive_id} awake refreshed → kau={keep_alive_until_str}", flush=True)
        return

    new_stop = threading.Event()
    state = {"stop": new_stop, "keep_alive_until": keep_alive_until_str}
    _awake[hive_id] = state

    def loop():
        # Simulate wake-from-deep-sleep delay before the first awake event.
        print(f"[responder] {hive_id} waking (delay {WAKE_DELAY_SEC}s)…", flush=True)
        if new_stop.wait(WAKE_DELAY_SEC):
            _awake.pop(hive_id, None)
            return
        # Initial heartbeat.
        _publish_awake(hive_id, state["keep_alive_until"])
        # Subsequent heartbeats every HEARTBEAT_INTERVAL_SEC until cleared
        # or the keep_alive_until in state has passed.
        while not new_stop.is_set():
            if new_stop.wait(HEARTBEAT_INTERVAL_SEC):
                break
            try:
                kau = parse_iso(state["keep_alive_until"])
            except Exception:
                break
            if datetime.now(timezone.utc) >= kau:
                print(f"[responder] {hive_id} keep_alive_until expired, exiting awake mode", flush=True)
                break
            _publish_awake(hive_id, state["keep_alive_until"])
        _awake.pop(hive_id, None)

    threading.Thread(target=loop, daemon=True).start()

def _publish_awake(hive_id, keep_alive_until_str):
    event = {
        "event": "awake",
        "keep_alive_until": keep_alive_until_str,
        "ts": now_iso(),
    }
    _publish(f"combsense/hive/{hive_id}/scale/status", json.dumps(event))
    print(f"[responder] {hive_id} → awake (kau={keep_alive_until_str})", flush=True)

def _exit_awake(hive_id):
    state = _awake.get(hive_id)
    if state:
        state["stop"].set()
        print(f"[responder] {hive_id} awake cleared", flush=True)

def handle_config(hive_id, payload):
    """Process a scale/config message. Empty payload clears, non-empty enters
    extended-awake mode."""
    if not payload:
        _exit_awake(hive_id)
        return
    try:
        cfg = json.loads(payload)
    except json.JSONDecodeError:
        print(f"[responder] bad config JSON: {payload}", flush=True)
        return
    kau_str = cfg.get("keep_alive_until")
    if not kau_str:
        print(f"[responder] config missing keep_alive_until: {payload}", flush=True)
        return
    try:
        kau = parse_iso(kau_str)
    except Exception:
        print(f"[responder] bad keep_alive_until: {kau_str}", flush=True)
        return
    if kau <= datetime.now(timezone.utc):
        print(f"[responder] keep_alive_until in the past, treating as clear", flush=True)
        _exit_awake(hive_id)
        return
    _enter_awake(hive_id, kau_str)

def respond(hive_id: str, cmd: dict):
    name = cmd.get("cmd")
    if name == "tare":
        event = {"event": "tare_saved", "raw_offset": 1234567, "ts": now_iso()}
    elif name == "calibrate":
        event = {
            "event": "calibration_saved",
            "scale_factor": 4567.89,
            "predicted_accuracy_pct": 1.8,
            "ts": now_iso(),
        }
    elif name == "verify":
        expected = cmd.get("expected_kg", 5.0)
        measured = round(expected * 1.006, 2)
        event = {
            "event": "verify_result",
            "measured_kg": measured,
            "expected_kg": expected,
            "error_pct": 0.6,
            "ts": now_iso(),
        }
    elif name == "modify_start":
        event = {
            "event": "modify_started",
            "label": cmd.get("label", "unknown"),
            "pre_event_kg": 47.3,
            "ts": now_iso(),
        }
    elif name == "modify_end":
        event = {
            "event": "modify_complete",
            "label": cmd.get("label", "unknown"),
            "pre_kg": 47.3, "post_kg": 58.1, "delta_kg": 10.8,
            "duration_sec": 287, "tare_updated": True, "ts": now_iso(),
        }
    elif name == "stream_raw":
        duration = int(cmd.get("duration_sec", 60))
        _start_stream(hive_id, duration)
        return
    elif name == "stop_stream":
        _stop_stream(hive_id)
        return
    elif name == "modify_cancel":
        return
    else:
        print(f"[responder] unknown cmd: {name}", flush=True)
        return

    payload = json.dumps(event)
    topic = f"combsense/hive/{hive_id}/scale/status"
    _publish(topic, payload)
    print(f"[responder] {topic}  {payload}", flush=True)

def main():
    print(f"[responder] starting — sub: combsense/hive/+/scale/cmd + scale/config", flush=True)
    print(f"[responder] WAKE_DELAY_SEC={WAKE_DELAY_SEC}  HEARTBEAT_INTERVAL_SEC={HEARTBEAT_INTERVAL_SEC}", flush=True)
    proc = subprocess.Popen(
        ["mosquitto_sub", "-h", BROKER, "-p", PORT,
         "-u", USER, "-P", PASS,
         "-t", "combsense/hive/+/scale/cmd",
         "-t", "combsense/hive/+/scale/config",
         "-v"],
        stdout=subprocess.PIPE, text=True,
    )
    try:
        for line in proc.stdout:
            line = line.rstrip("\n")
            if not line:
                continue
            # Topic and payload are space-separated; payload may be empty
            # (retained-clear case is "topic " with no payload after the space).
            if " " in line:
                topic, payload = line.split(" ", 1)
            else:
                topic, payload = line, ""
            try:
                parts = topic.split("/")
                hive_id = parts[2]
                kind = parts[4]   # "cmd" or "config"
            except IndexError:
                print(f"[responder] malformed topic: {topic}", flush=True)
                continue
            print(f"[responder] {topic}  ←  {payload!r}", flush=True)
            time.sleep(0.05)
            if kind == "config":
                handle_config(hive_id, payload)
                continue
            if kind == "cmd":
                if not payload:
                    print(f"[responder] empty cmd payload — ignoring", flush=True)
                    continue
                try:
                    cmd = json.loads(payload)
                except json.JSONDecodeError:
                    print(f"[responder] bad cmd JSON: {payload}", flush=True)
                    continue
                respond(hive_id, cmd)
    except KeyboardInterrupt:
        proc.terminate()

if __name__ == "__main__":
    main()
