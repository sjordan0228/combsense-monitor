#pragma once

#include <cstdint>

#define TINY_GSM_MODEM_SIM7080
#include <TinyGsmClient.h>

namespace Cellular {
    bool powerOn();
    void powerOff();
    bool waitForNetwork();
    uint32_t syncNtp();
    TinyGsm& getModem();
    TinyGsmClient& getClient();
}
