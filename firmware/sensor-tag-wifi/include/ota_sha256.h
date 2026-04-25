#pragma once

#include <cstddef>
#include <cstdint>

class Sha256Streamer {
public:
    Sha256Streamer();
    ~Sha256Streamer();

    void reset();
    void update(const uint8_t* data, size_t len);
    void finalizeToHex(char outHex[65]);   // writes 64 lowercase hex + null

    bool matches(const char* expectedHex);  // call after finalizeToHex

private:
    void* impl_;
    char lastHex_[65];
};
