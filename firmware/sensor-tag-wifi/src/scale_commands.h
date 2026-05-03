#pragma once

#include <cstdint>

enum class ScaleCommandType : uint8_t {
    Tare,
    Calibrate,
    Verify,
    StreamRaw,
    StopStream,
    ModifyStart,
    ModifyEnd,
    ModifyCancel,
};

struct ScaleCommand {
    ScaleCommandType type;
    union {
        struct { double known_kg; }        calibrate;
        struct { double expected_kg; }     verify;
        struct { int32_t duration_sec; }   stream_raw;
        struct { char label[32]; }         modify;
    };
};

bool parseScaleCommand(const char* json, ScaleCommand& out);
