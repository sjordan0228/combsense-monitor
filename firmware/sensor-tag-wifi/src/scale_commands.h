#pragma once

#include <cstddef>
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

// Status event serializers. Each writes a complete JSON object to `buf` and
// returns the number of bytes written (excl null), or 0 on overflow.

int serializeAwakeEvent(int64_t keep_alive_until, int64_t ts,
                        char* buf, size_t bufsz);
int serializeTareSavedEvent(int64_t raw_offset, int64_t ts,
                            char* buf, size_t bufsz);
int serializeCalibrationSavedEvent(double scale_factor, double predicted_accuracy_pct,
                                   int64_t ts, char* buf, size_t bufsz);
int serializeVerifyResultEvent(double measured_kg, double expected_kg, double error_pct,
                               int64_t ts, char* buf, size_t bufsz);
int serializeRawStreamEvent(int32_t raw_value, double kg, bool stable,
                            int64_t ts, char* buf, size_t bufsz);
int serializeModifyStartedEvent(const char* label, double pre_event_kg,
                                int64_t ts, char* buf, size_t bufsz);
int serializeModifyCompleteEvent(const char* label, double pre_kg, double post_kg,
                                 double delta_kg, int32_t duration_sec, bool tare_updated,
                                 int64_t ts, char* buf, size_t bufsz);
int serializeModifyWarningEvent(const char* label, double delta_kg, const char* warning,
                                int64_t ts, char* buf, size_t bufsz);
int serializeModifyTimeoutEvent(const char* label, int64_t ts, char* buf, size_t bufsz);
int serializeErrorEvent(const char* code, const char* details,
                        int64_t ts, char* buf, size_t bufsz);
