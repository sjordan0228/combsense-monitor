#ifdef SENSOR_SCALE

#include "scale.h"
#include "scale_math.h"
#include "scale_commands.h"
#include "config.h"
#include "mqtt_client.h"
#include "payload.h"

#include <Arduino.h>
#include <HX711.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <cstring>
#include <time.h>

namespace {

HX711 hx711;
Preferences prefs;

int64_t weight_off_   = 0;
double  weight_scl_   = HX711_DEFAULT_SCALE_FACTOR;
StableDetector stable_;

bool extended_awake_ = false;
int64_t keep_alive_until_ = 0;
uint32_t last_heartbeat_ms_ = 0;

bool streaming_ = false;
int64_t stream_until_ = 0;
uint32_t last_stream_ms_ = 0;

bool modify_active_ = false;
char modify_label_[32] = {};
double modify_pre_kg_ = 0.0;
int64_t modify_started_at_ = 0;
int64_t modify_timeout_at_ = 0;

constexpr const char* NVS_NS = "combsense";
constexpr const char* NVS_K_OFF = "weight_off";
constexpr const char* NVS_K_SCL = "weight_scl";

int64_t nowEpoch() {
    time_t t = time(nullptr);
    return static_cast<int64_t>(t);
}

void loadFromNvs() {
    prefs.begin(NVS_NS, /*readOnly=*/true);
    weight_off_ = prefs.getLong64(NVS_K_OFF, 0);
    weight_scl_ = prefs.getDouble(NVS_K_SCL, HX711_DEFAULT_SCALE_FACTOR);
    prefs.end();
}

void writeOffsetToNvs(int64_t off) {
    prefs.begin(NVS_NS, /*readOnly=*/false);
    prefs.putLong64(NVS_K_OFF, off);
    prefs.end();
    weight_off_ = off;
}

void writeScaleToNvs(double scl) {
    prefs.begin(NVS_NS, /*readOnly=*/false);
    prefs.putDouble(NVS_K_SCL, scl);
    prefs.end();
    weight_scl_ = scl;
}

}  // anonymous namespace

namespace Scale {

void init() {
    hx711.begin(PIN_HX711_DT_, PIN_HX711_SCK_, HX711_GAIN);
    hx711.power_up();
    loadFromNvs();
    stable_.reset();
    extended_awake_ = false;
    keep_alive_until_ = 0;
    streaming_ = false;
    modify_active_ = false;
}

void deinit() {
    hx711.power_down();
}

bool sample(int32_t& raw, double& kg) {
    if (!hx711.wait_ready_timeout(HX711_READ_TIMEOUT_MS)) {
        kg = NAN;
        raw = 0;
        return false;
    }
    raw = hx711.read();
    stable_.push(raw);
    kg = applyCalibration(raw, weight_off_, weight_scl_);
    return true;
}

void subscribe() {}

void onMessage(const char* /*topic*/, const char* /*payload*/, unsigned int /*len*/) {}

void onConnect() {}

void tick() {}

bool ntpSynced() {
    // Heuristic: if epoch is well past 2020, NTP has fired.
    return nowEpoch() > 1577836800;  // 2020-01-01 UTC
}

bool inExtendedAwakeMode() {
    return extended_awake_;
}

int64_t keepAliveUntil() {
    return keep_alive_until_;
}

}  // namespace Scale

#else  // !SENSOR_SCALE — provide no-op stubs so main.cpp compiles unchanged

#include "scale.h"
#include <cstdint>
#include <cmath>

namespace Scale {
void init() {}
void deinit() {}
bool sample(int32_t&, double& kg) { kg = NAN; return false; }
void subscribe() {}
void onMessage(const char*, const char*, unsigned int) {}
void tick() {}
bool inExtendedAwakeMode() { return false; }
int64_t keepAliveUntil() { return 0; }
void onConnect() {}
bool ntpSynced() { return true; }
}

#endif  // SENSOR_SCALE
