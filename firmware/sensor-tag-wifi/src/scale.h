#pragma once

#include <cstdint>

namespace Scale {

void init();
void deinit();
bool sample(int32_t& raw, double& kg);           // false on HX711 timeout
bool sampleAveraged(uint8_t n, int32_t& raw_mean, double& kg);  // trimmed mean (drops hi/lo)

void subscribe();                         // call after MQTT connect
void onMessage(const char* topic, const char* payload, unsigned int len);
void tick();                              // call from extended-awake loop
bool inExtendedAwakeMode();
int64_t keepAliveUntil();

// Convenience for main.cpp
void onConnect();       // subscribe + check retained config (1.5s grace window)
bool ntpSynced();       // helper used to gate extended-awake entry
bool isCalibrated();    // true when NVS scale factor differs from default

}  // namespace Scale
