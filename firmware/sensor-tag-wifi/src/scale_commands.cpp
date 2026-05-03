#include "scale_commands.h"
#include "scale_math.h"
#include <ArduinoJson.h>
#include <cstring>

bool parseScaleCommand(const char* json, ScaleCommand& out) {
    JsonDocument doc;
    auto err = deserializeJson(doc, json);
    if (err) return false;

    const char* cmd = doc["cmd"].as<const char*>();
    if (!cmd) return false;

    if (strcmp(cmd, "tare") == 0) {
        out.type = ScaleCommandType::Tare;
        return true;
    }
    if (strcmp(cmd, "calibrate") == 0) {
        if (!doc["known_kg"].is<double>()) return false;
        out.type = ScaleCommandType::Calibrate;
        out.calibrate.known_kg = doc["known_kg"].as<double>();
        return true;
    }
    if (strcmp(cmd, "verify") == 0) {
        if (!doc["expected_kg"].is<double>()) return false;
        out.type = ScaleCommandType::Verify;
        out.verify.expected_kg = doc["expected_kg"].as<double>();
        return true;
    }
    if (strcmp(cmd, "stream_raw") == 0) {
        if (!doc["duration_sec"].is<int32_t>()) return false;
        out.type = ScaleCommandType::StreamRaw;
        out.stream_raw.duration_sec = doc["duration_sec"].as<int32_t>();
        return true;
    }
    if (strcmp(cmd, "stop_stream") == 0) {
        out.type = ScaleCommandType::StopStream;
        return true;
    }
    if (strcmp(cmd, "modify_start") == 0 || strcmp(cmd, "modify_end") == 0) {
        const char* label = doc["label"].as<const char*>();
        if (!label) return false;
        out.type = (strcmp(cmd, "modify_start") == 0)
            ? ScaleCommandType::ModifyStart
            : ScaleCommandType::ModifyEnd;
        std::strncpy(out.modify.label, label, sizeof(out.modify.label) - 1);
        out.modify.label[sizeof(out.modify.label) - 1] = '\0';
        return true;
    }
    if (strcmp(cmd, "modify_cancel") == 0) {
        out.type = ScaleCommandType::ModifyCancel;
        return true;
    }
    return false;
}

namespace {
void writeTs(JsonDocument& doc, int64_t ts) {
    char buf[24];
    formatRFC3339(ts, buf, sizeof(buf));
    doc["ts"] = buf;
}
} // namespace

int serializeAwakeEvent(int64_t keep_alive_until, int64_t ts, char* buf, size_t bufsz) {
    JsonDocument doc;
    doc["event"] = "awake";
    char kau[24];
    formatRFC3339(keep_alive_until, kau, sizeof(kau));
    doc["keep_alive_until"] = kau;
    writeTs(doc, ts);
    return serializeJson(doc, buf, bufsz);
}

int serializeTareSavedEvent(int64_t raw_offset, int64_t ts, char* buf, size_t bufsz) {
    JsonDocument doc;
    doc["event"] = "tare_saved";
    doc["raw_offset"] = raw_offset;
    writeTs(doc, ts);
    return serializeJson(doc, buf, bufsz);
}

int serializeCalibrationSavedEvent(double scale_factor, double predicted_accuracy_pct,
                                   int64_t ts, char* buf, size_t bufsz) {
    JsonDocument doc;
    doc["event"] = "calibration_saved";
    doc["scale_factor"] = scale_factor;
    doc["predicted_accuracy_pct"] = predicted_accuracy_pct;
    writeTs(doc, ts);
    return serializeJson(doc, buf, bufsz);
}

int serializeVerifyResultEvent(double measured_kg, double expected_kg, double error_pct_,
                               int64_t ts, char* buf, size_t bufsz) {
    JsonDocument doc;
    doc["event"] = "verify_result";
    doc["measured_kg"] = measured_kg;
    doc["expected_kg"] = expected_kg;
    doc["error_pct"] = error_pct_;
    writeTs(doc, ts);
    return serializeJson(doc, buf, bufsz);
}

int serializeRawStreamEvent(int32_t raw_value, double kg, bool stable,
                            int64_t ts, char* buf, size_t bufsz) {
    JsonDocument doc;
    doc["event"] = "raw_stream";
    doc["raw_value"] = raw_value;
    doc["kg"] = kg;
    doc["stable"] = stable;
    writeTs(doc, ts);
    return serializeJson(doc, buf, bufsz);
}

int serializeModifyStartedEvent(const char* label, double pre_event_kg,
                                int64_t ts, char* buf, size_t bufsz) {
    JsonDocument doc;
    doc["event"] = "modify_started";
    doc["label"] = label;
    doc["pre_event_kg"] = pre_event_kg;
    writeTs(doc, ts);
    return serializeJson(doc, buf, bufsz);
}

int serializeModifyCompleteEvent(const char* label, double pre_kg, double post_kg,
                                 double delta_kg, int32_t duration_sec, bool tare_updated,
                                 int64_t ts, char* buf, size_t bufsz) {
    JsonDocument doc;
    doc["event"] = "modify_complete";
    doc["label"] = label;
    doc["pre_kg"] = pre_kg;
    doc["post_kg"] = post_kg;
    doc["delta_kg"] = delta_kg;
    doc["duration_sec"] = duration_sec;
    doc["tare_updated"] = tare_updated;
    writeTs(doc, ts);
    return serializeJson(doc, buf, bufsz);
}

int serializeModifyWarningEvent(const char* label, double delta_kg, const char* warning,
                                int64_t ts, char* buf, size_t bufsz) {
    JsonDocument doc;
    doc["event"] = "modify_warning";
    doc["label"] = label;
    doc["delta_kg"] = delta_kg;
    doc["warning"] = warning;
    writeTs(doc, ts);
    return serializeJson(doc, buf, bufsz);
}

int serializeModifyTimeoutEvent(const char* label, int64_t ts, char* buf, size_t bufsz) {
    JsonDocument doc;
    doc["event"] = "modify_timeout";
    doc["label"] = label;
    writeTs(doc, ts);
    return serializeJson(doc, buf, bufsz);
}

int serializeErrorEvent(const char* code, const char* details,
                        int64_t ts, char* buf, size_t bufsz) {
    JsonDocument doc;
    doc["event"] = "error";
    doc["code"] = code;
    doc["details"] = details;
    writeTs(doc, ts);
    return serializeJson(doc, buf, bufsz);
}
