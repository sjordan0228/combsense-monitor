#pragma once

#include <cstdint>

namespace Scale {

void init();
void deinit();
bool sample(int32_t& raw, double& kg);   // false on HX711 timeout

void subscribe();                         // call after MQTT connect
void onMessage(const char* topic, const char* payload, unsigned int len);
void tick();                              // call from extended-awake loop
bool inExtendedAwakeMode();
int64_t keepAliveUntil();

// Convenience for main.cpp
void onConnect();   // subscribe + check retained config (1.5s grace window)
bool ntpSynced();   // helper used to gate extended-awake entry

}  // namespace Scale
