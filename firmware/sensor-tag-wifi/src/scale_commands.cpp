#include "scale_commands.h"
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
