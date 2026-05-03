#include "config_parser.h"

#include <ArduinoJson.h>
#include <cstring>

namespace ConfigParser {

namespace {

bool isExcludedKey(const char* key) {
    return strcmp(key, "wifi_ssid")  == 0 ||
           strcmp(key, "wifi_pass")  == 0 ||
           strcmp(key, "mqtt_host")  == 0 ||
           strcmp(key, "mqtt_port")  == 0 ||
           strcmp(key, "mqtt_user")  == 0 ||
           strcmp(key, "mqtt_pass")  == 0;
}

void recordReject(ConfigUpdate& out, const char* key, const char* reason) {
    if (out.num_rejected >= MAX_REJECTED_KEYS) return;
    ConfigUpdate::RejectedKey& slot = out.rejected[out.num_rejected];
    size_t kn = strlen(key);
    if (kn >= REJECTED_KEY_LEN) kn = REJECTED_KEY_LEN - 1;
    memcpy(slot.key, key, kn);
    slot.key[kn] = '\0';
    size_t rn = strlen(reason);
    if (rn >= REJECTED_REASON_LEN) rn = REJECTED_REASON_LEN - 1;
    memcpy(slot.reason, reason, rn);
    slot.reason[rn] = '\0';
    out.num_rejected += 1;
}

void copyString(char* dst, size_t dstCap, const char* src) {
    size_t n = strlen(src);
    if (n >= dstCap) n = dstCap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

bool isAllowedStringValue(const char* s, size_t maxLen) {
    if (s == nullptr) return false;
    return strlen(s) < maxLen;  // strict-less so we have room for NUL
}

}  // namespace

/// Parse a feat_* integer value that must be exactly 0 or 1.
/// Returns FeatFlag::Absent on wrong type, FeatFlag::Off/On on valid value.
/// Records key in rejected list with the "invalid:not_0_or_1" suffix on bad range.
static FeatFlag parseFeatFlag(JsonVariant val, const char* key, ConfigUpdate& out) {
    if (!val.is<int>()) {
        recordReject(out, key, "invalid:not_0_or_1");
        return FeatFlag::Absent;
    }
    int v = val.as<int>();
    if (v != 0 && v != 1) {
        recordReject(out, key, "invalid:not_0_or_1");
        return FeatFlag::Absent;
    }
    return (v == 1) ? FeatFlag::On : FeatFlag::Off;
}

bool parse(const char* json, ConfigUpdate& out) {
    memset(&out, 0, sizeof(out));
    // FeatFlag fields need explicit Absent initialisation (memset zeros them,
    // but Absent = 0xFF so we must set explicitly after the memset).
    out.feat_ds18b20 = FeatFlag::Absent;
    out.feat_sht31   = FeatFlag::Absent;
    out.feat_scale   = FeatFlag::Absent;
    out.feat_mic     = FeatFlag::Absent;

    if (json == nullptr) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) return false;
    if (!doc.is<JsonObject>()) return false;

    JsonObject root = doc.as<JsonObject>();

    for (JsonPair kv : root) {
        const char* key = kv.key().c_str();

        // sample_int
        if (strcmp(key, "sample_int") == 0) {
            if (!kv.value().is<int>()) {
                recordReject(out, key, "invalid:wrong_type");
                continue;
            }
            int v = kv.value().as<int>();
            if (v < SAMPLE_INT_MIN || v > SAMPLE_INT_MAX) {
                recordReject(out, key, "invalid:out_of_range");
                continue;
            }
            out.has_sample_int = true;
            out.sample_int     = static_cast<uint16_t>(v);
            continue;
        }

        // upload_every
        if (strcmp(key, "upload_every") == 0) {
            if (!kv.value().is<int>()) {
                recordReject(out, key, "invalid:wrong_type");
                continue;
            }
            int v = kv.value().as<int>();
            if (v < UPLOAD_EVERY_MIN || v > UPLOAD_EVERY_MAX) {
                recordReject(out, key, "invalid:out_of_range");
                continue;
            }
            out.has_upload_every = true;
            out.upload_every     = static_cast<uint8_t>(v);
            continue;
        }

        // tag_name
        if (strcmp(key, "tag_name") == 0) {
            if (!kv.value().is<const char*>()) {
                recordReject(out, key, "invalid:wrong_type");
                continue;
            }
            const char* s = kv.value().as<const char*>();
            if (!isAllowedStringValue(s, TAG_NAME_MAX_LEN)) {
                recordReject(out, key, "invalid:string_too_long");
                continue;
            }
            out.has_tag_name = true;
            copyString(out.tag_name, TAG_NAME_MAX_LEN, s);
            continue;
        }

        // ota_host
        if (strcmp(key, "ota_host") == 0) {
            if (!kv.value().is<const char*>()) {
                recordReject(out, key, "invalid:wrong_type");
                continue;
            }
            const char* s = kv.value().as<const char*>();
            if (!isAllowedStringValue(s, OTA_HOST_MAX_LEN)) {
                recordReject(out, key, "invalid:string_too_long");
                continue;
            }
            out.has_ota_host = true;
            copyString(out.ota_host, OTA_HOST_MAX_LEN, s);
            continue;
        }

        // feat_* flags — each accepts exactly 0 or 1
        if (strcmp(key, "feat_ds18b20") == 0) {
            FeatFlag f = parseFeatFlag(kv.value(), key, out);
            if (f != FeatFlag::Absent) out.feat_ds18b20 = f;
            continue;
        }
        if (strcmp(key, "feat_sht31") == 0) {
            FeatFlag f = parseFeatFlag(kv.value(), key, out);
            if (f != FeatFlag::Absent) out.feat_sht31 = f;
            continue;
        }
        if (strcmp(key, "feat_scale") == 0) {
            FeatFlag f = parseFeatFlag(kv.value(), key, out);
            if (f != FeatFlag::Absent) out.feat_scale = f;
            continue;
        }
        if (strcmp(key, "feat_mic") == 0) {
            FeatFlag f = parseFeatFlag(kv.value(), key, out);
            if (f != FeatFlag::Absent) out.feat_mic = f;
            continue;
        }

        // Anything else — excluded-by-policy key or genuinely unknown key.
        // §6: report distinct reasons so the ack can carry the right category.
        if (isExcludedKey(key)) {
            char reason[REJECTED_REASON_LEN];
            snprintf(reason, sizeof(reason), "excluded:%s", key);
            recordReject(out, key, reason);
        } else {
            recordReject(out, key, "unknown_key");
        }
    }

    return true;
}

}  // namespace ConfigParser
