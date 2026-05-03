#pragma once

#include "config_parser.h"
#include <cstddef>
#include <cstdint>

/// Unified per-key result record for the config ack pipeline.
///
/// Full `category:detail` vocabulary per config-mqtt-contract.md §6:
///   "ok", "unchanged", "unknown_key", "excluded:<reason>",
///   "invalid:<reason>", "conflict:<other_key>"
struct AckEntry {
    char key[16];
    char result[32];
};

/// Current NVS state of the two mutually-exclusive temperature sensor flags.
/// Passed to preValidate so the function is pure C++ (no NVS I/O) and
/// fully testable in the native env.
struct TemperatureNvsState {
    bool ds18b20_enabled;  ///< current NVS value (or compile-time default)
    bool sht31_enabled;    ///< current NVS value (or compile-time default)
};

/// Pre-validation hook: enforces cross-key constraints before the NVS apply
/// loop.  Must be called with the fully-parsed ConfigUpdate and the current
/// NVS state of the mutually-exclusive temp-sensor flags.
///
/// Conflict rule: if the post-apply state would have both feat_ds18b20 AND
/// feat_sht31 enabled, both keys are appended to outEntries with the
/// appropriate "conflict:<other_key>" result, and false is returned.
///
/// Returns true when the caller may proceed to applyConfigToNvs.
bool preValidate(const ConfigParser::ConfigUpdate& parsed,
                 const TemperatureNvsState& nvsState,
                 AckEntry* outEntries, size_t* outCount);
