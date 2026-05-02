#pragma once

#include <cstdint>
#include "config.h"

class StableDetector {
public:
    StableDetector() = default;
    void push(int32_t raw);
    bool isStable() const;
    void reset();

private:
    int32_t ring_[HX711_STABLE_WINDOW_LEN] = {};
    uint8_t count_ = 0;
    uint8_t head_  = 0;
};
