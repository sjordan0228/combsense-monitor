#pragma once

#include <cstdint>

namespace Config {

/// Returns true if the named feature flag is enabled.
/// Reads NVS first; on key-absent or zero-value returns the compile-time
/// default for that feature.
/// Valid names: "feat_ds18b20", "feat_sht31", "feat_scale", "feat_mic".
bool isEnabled(const char* name);

/// Convenience getter for ints with a caller-supplied default.
/// Reads NVS, falls back to default_value if key is absent.
int32_t getInt(const char* name, int32_t default_value);

}  // namespace Config
