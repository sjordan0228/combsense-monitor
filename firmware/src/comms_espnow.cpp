// STUB — replaced in Task 8
#include "comms_espnow.h"
#include <Arduino.h>
namespace CommsEspNow {
    bool initialize() { Serial.println("[ESPNOW] STUB"); return true; }
    bool sendPayload(HivePayload&) { return false; }
    void shutdown() {}
}
