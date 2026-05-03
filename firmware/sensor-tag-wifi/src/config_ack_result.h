#pragma once

/// Named constants for per-key result categories per config-mqtt-contract.md §6.
/// Intentionally dependency-free so both config_parser.cpp and config_ack.cpp
/// can include it without introducing circular dependencies.
namespace AckResult {
    constexpr const char* OK                = "ok";
    constexpr const char* UNCHANGED         = "unchanged";
    constexpr const char* UNKNOWN_KEY       = "unknown_key";
    constexpr const char* INVALID_NVS_WRITE = "invalid:nvs_write_failed";
    // Prefix constants for dynamically constructed results (snprintf).
    constexpr const char* EXCLUDED_PREFIX   = "excluded:";
    constexpr const char* INVALID_PREFIX    = "invalid:";
    constexpr const char* CONFLICT_PREFIX   = "conflict:";
}
