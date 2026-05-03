#include "scale_math.h"
#include <algorithm>
#include <cmath>
#include <ctime>

void StableDetector::push(int32_t raw) {
    ring_[head_] = raw;
    head_ = (head_ + 1) % HX711_STABLE_WINDOW_LEN;
    if (count_ < HX711_STABLE_WINDOW_LEN) count_++;
}

bool StableDetector::isStable() const {
    if (count_ < HX711_STABLE_WINDOW_LEN) return false;
    int32_t mn = ring_[0], mx = ring_[0];
    for (uint8_t i = 1; i < HX711_STABLE_WINDOW_LEN; i++) {
        mn = std::min(mn, ring_[i]);
        mx = std::max(mx, ring_[i]);
    }
    return (mx - mn) <= HX711_STABLE_TOLERANCE_RAW;
}

void StableDetector::reset() {
    count_ = 0;
    head_  = 0;
}

double applyCalibration(int32_t raw, int64_t off, double scale_factor) {
    if (scale_factor == 0.0) return std::nan("");
    return static_cast<double>(static_cast<int64_t>(raw) - off) / scale_factor;
}

int64_t tareFromMean(const int32_t* samples, uint8_t n) {
    if (n == 0) return 0;
    int64_t sum = 0;
    for (uint8_t i = 0; i < n; i++) sum += samples[i];
    return sum / n;
}

double scaleFactorFromMean(const int32_t* samples, uint8_t n, int64_t off, double known_kg) {
    if (n == 0 || known_kg == 0.0) return 0.0;
    int64_t sum = 0;
    for (uint8_t i = 0; i < n; i++) sum += samples[i];
    double mean = static_cast<double>(sum) / n;
    return (mean - static_cast<double>(off)) / known_kg;
}

double errorPct(double measured, double expected) {
    if (expected == 0.0) return -1.0;
    return std::fabs(measured - expected) / std::fabs(expected) * 100.0;
}

bool isKeepAliveValid(int64_t keep_alive_until, int64_t now) {
    if (keep_alive_until <= 0) return false;
    if (keep_alive_until > now) return true;
    return keep_alive_until > (now - CLOCK_SKEW_TOLERANCE_SEC);
}

size_t formatRFC3339(int64_t epoch, char* buf, size_t bufsz) {
    if (bufsz < 21) return 0;
    time_t t = static_cast<time_t>(epoch);
    struct tm utc;
    gmtime_r(&t, &utc);
    return strftime(buf, bufsz, "%Y-%m-%dT%H:%M:%SZ", &utc);
}
