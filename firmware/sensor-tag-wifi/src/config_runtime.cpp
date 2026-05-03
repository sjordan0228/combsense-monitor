#include "config_runtime.h"
#include "config.h"
#include <cstring>

// ---- compile-time feature defaults -----------------------------------------
#ifndef DEFAULT_FEAT_DS18B20
#define DEFAULT_FEAT_DS18B20 1
#endif
#ifndef DEFAULT_FEAT_SHT31
#define DEFAULT_FEAT_SHT31 0
#endif
#ifndef DEFAULT_FEAT_SCALE
#define DEFAULT_FEAT_SCALE 0
#endif
#ifndef DEFAULT_FEAT_MIC
#define DEFAULT_FEAT_MIC 0
#endif

// ----------------------------------------------------------------------------

#ifdef ARDUINO
#include <Preferences.h>

namespace Config {

int32_t getInt(const char* name, int32_t default_value) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, /*readOnly=*/true);
    int32_t val = prefs.getInt(name, default_value);
    prefs.end();
    return val;
}

}  // namespace Config

#else  // native / unit-test build — NVS not available, always return default

namespace Config {

int32_t getInt(const char* name, int32_t default_value) {
    (void)name;
    return default_value;
}

}  // namespace Config

#endif  // ARDUINO

// isEnabled is compile-time-default driven; the NVS read path is the same
// for both embedded and (via getInt stub) native.
namespace Config {

static int32_t compiletimeDefault(const char* name) {
    if (strcmp(name, "feat_ds18b20") == 0) return DEFAULT_FEAT_DS18B20;
    if (strcmp(name, "feat_sht31")   == 0) return DEFAULT_FEAT_SHT31;
    if (strcmp(name, "feat_scale")   == 0) return DEFAULT_FEAT_SCALE;
    if (strcmp(name, "feat_mic")     == 0) return DEFAULT_FEAT_MIC;
    return 0;  // unknown feature — off by default
}

bool isEnabled(const char* name) {
    int32_t def = compiletimeDefault(name);
    return getInt(name, def) != 0;
}

}  // namespace Config
