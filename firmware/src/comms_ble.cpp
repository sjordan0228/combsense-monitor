// STUB — replaced in Task 9
#include "comms_ble.h"
#include <Arduino.h>
namespace CommsBle {
    bool initialize() { Serial.println("[BLE] STUB"); return true; }
    bool advertiseAndWait(uint16_t) { return false; }
    void waitForSyncComplete() {}
    void shutdown() {}
}
