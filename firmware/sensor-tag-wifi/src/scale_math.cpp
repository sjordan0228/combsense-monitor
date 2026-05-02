#include "scale_math.h"
#include <algorithm>

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
