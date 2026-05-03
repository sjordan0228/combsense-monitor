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

    // Compute median of the window
    int32_t sorted[HX711_STABLE_WINDOW_LEN];
    for (uint8_t i = 0; i < HX711_STABLE_WINDOW_LEN; i++) sorted[i] = ring_[i];
    std::sort(sorted, sorted + HX711_STABLE_WINDOW_LEN);
    // For even N: average of two middle elements
    int64_t med2 = static_cast<int64_t>(sorted[HX711_STABLE_WINDOW_LEN / 2 - 1]) +
                   static_cast<int64_t>(sorted[HX711_STABLE_WINDOW_LEN / 2]);

    // Compute absolute deviations from median (scaled by 2 to avoid fractional median)
    int32_t devs[HX711_STABLE_WINDOW_LEN];
    for (uint8_t i = 0; i < HX711_STABLE_WINDOW_LEN; i++) {
        int64_t diff = 2LL * static_cast<int64_t>(ring_[i]) - med2;
        devs[i] = static_cast<int32_t>(diff < 0 ? -diff : diff);
    }
    std::sort(devs, devs + HX711_STABLE_WINDOW_LEN);
    // MAD (scaled by 2) = median of |sample - median| * 2
    int64_t mad2 = static_cast<int64_t>(devs[HX711_STABLE_WINDOW_LEN / 2 - 1]) +
                   static_cast<int64_t>(devs[HX711_STABLE_WINDOW_LEN / 2]);
    // Stable if MAD <= tolerance (both scaled by 2, so compare mad2 <= tolerance*2)
    return mad2 <= static_cast<int64_t>(HX711_STABLE_TOLERANCE_RAW) * 2;
}

void StableDetector::reset() {
    count_ = 0;
    head_  = 0;
}

double applyCalibration(int32_t raw, int64_t off, double scale_factor) {
    if (scale_factor == 0.0) return std::nan("");
    return static_cast<double>(static_cast<int64_t>(raw) - off) / scale_factor;
}

int64_t tareFromMean(const int32_t* samples, uint8_t n, uint8_t trim) {
    if (n == 0) return 0;
    // Clamp trim so at least 1 sample remains
    uint8_t drop = 2 * trim;
    if (drop >= n) drop = 0;

    // Sort a copy
    int32_t sorted[HX711_TARE_SAMPLE_COUNT > HX711_VERIFY_SAMPLE_COUNT
                   ? HX711_TARE_SAMPLE_COUNT : HX711_VERIFY_SAMPLE_COUNT];
    uint8_t sz = n < sizeof(sorted) / sizeof(sorted[0]) ? n : sizeof(sorted) / sizeof(sorted[0]);
    for (uint8_t i = 0; i < sz; i++) sorted[i] = samples[i];
    std::sort(sorted, sorted + sz);

    uint8_t lo = trim < sz ? trim : 0;
    uint8_t hi = (sz > trim) ? (sz - trim) : sz;
    uint8_t used = hi - lo;
    if (used == 0) { used = sz; lo = 0; hi = sz; }

    int64_t sum = 0;
    for (uint8_t i = lo; i < hi; i++) sum += sorted[i];
    return sum / used;
}

double scaleFactorFromMean(const int32_t* samples, uint8_t n, int64_t off, double known_kg, uint8_t trim) {
    if (n == 0 || known_kg == 0.0) return 0.0;
    // Clamp trim so at least 1 sample remains
    uint8_t drop = 2 * trim;
    if (drop >= n) drop = 0;

    int32_t sorted[HX711_TARE_SAMPLE_COUNT > HX711_VERIFY_SAMPLE_COUNT
                   ? HX711_TARE_SAMPLE_COUNT : HX711_VERIFY_SAMPLE_COUNT];
    uint8_t sz = n < sizeof(sorted) / sizeof(sorted[0]) ? n : sizeof(sorted) / sizeof(sorted[0]);
    for (uint8_t i = 0; i < sz; i++) sorted[i] = samples[i];
    std::sort(sorted, sorted + sz);

    uint8_t lo = trim < sz ? trim : 0;
    uint8_t hi = (sz > trim) ? (sz - trim) : sz;
    uint8_t used = hi - lo;
    if (used == 0) { used = sz; lo = 0; hi = sz; }

    int64_t sum = 0;
    for (uint8_t i = lo; i < hi; i++) sum += sorted[i];
    double mean = static_cast<double>(sum) / used;
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
