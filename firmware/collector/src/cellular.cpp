#include "cellular.h"
#include "config.h"
#include <Arduino.h>
#include <ctime>

namespace {
    HardwareSerial modemSerial(1);
    TinyGsm modem(modemSerial);
    TinyGsmClient gsmClient(modem);
}  // anonymous namespace

namespace Cellular {

bool powerOn() {
    modemSerial.begin(MODEM_BAUD_RATE, SERIAL_8N1, PIN_MODEM_RXD, PIN_MODEM_TXD);

    // PWRKEY pulse to wake SIM7080G
    pinMode(PIN_MODEM_PWRKEY, OUTPUT);
    digitalWrite(PIN_MODEM_PWRKEY, LOW);
    delay(MODEM_PWRKEY_MS);
    digitalWrite(PIN_MODEM_PWRKEY, HIGH);
    delay(MODEM_BOOT_WAIT_MS);

    if (!modem.testAT(5000)) {
        Serial.printf("[CELL] modem AT check failed\n");
        return false;
    }

    String info = modem.getModemInfo();
    Serial.printf("[CELL] modem info: %s\n", info.c_str());
    return true;
}

void powerOff() {
    modem.poweroff();
}

bool waitForNetwork() {
    if (!modem.waitForNetwork(NETWORK_TIMEOUT_MS)) {
        Serial.printf("[CELL] network registration timed out\n");
        return false;
    }

    int16_t rssi = modem.getSignalQuality();
    Serial.printf("[CELL] network registered, RSSI=%d\n", rssi);
    return true;
}

uint32_t syncNtp() {
    // Configure NTP server and trigger sync
    modem.sendAT(GF("+CNTP=\"pool.ntp.org\",0"));
    if (modem.waitResponse(10000L) != 1) {
        Serial.printf("[CELL] AT+CNTP config failed\n");
        return 0;
    }

    modem.sendAT(GF("+CNTP"));
    if (modem.waitResponse(10000L) != 1) {
        Serial.printf("[CELL] AT+CNTP sync trigger failed\n");
        return 0;
    }

    // Allow the modem time to complete NTP sync
    delay(3000);

    String dt = modem.getGSMDateTime(DATE_FULL);
    if (dt.length() == 0) {
        Serial.printf("[CELL] failed to read GSM datetime\n");
        return 0;
    }

    Serial.printf("[CELL] GSM datetime: %s\n", dt.c_str());

    // Parse "YY/MM/DD,HH:MM:SS+TZ" — ignore timezone offset for UTC epoch
    int yy = 0, mo = 0, dd = 0, hh = 0, mm = 0, ss = 0;
    if (sscanf(dt.c_str(), "%d/%d/%d,%d:%d:%d", &yy, &mo, &dd, &hh, &mm, &ss) != 6) {
        Serial.printf("[CELL] datetime parse failed: %s\n", dt.c_str());
        return 0;
    }

    struct tm t = {};
    t.tm_year = (yy < 100 ? yy + 100 : yy);  // years since 1900; 2-digit yy + 100 = offset from 1900
    t.tm_mon  = mo - 1;                        // 0-based
    t.tm_mday = dd;
    t.tm_hour = hh;
    t.tm_min  = mm;
    t.tm_sec  = ss;

    time_t epoch = mktime(&t);
    if (epoch < 0) {
        Serial.printf("[CELL] mktime failed\n");
        return 0;
    }

    Serial.printf("[CELL] NTP epoch: %lu\n", (unsigned long)epoch);
    return static_cast<uint32_t>(epoch);
}

TinyGsm& getModem() {
    return modem;
}

TinyGsmClient& getClient() {
    return gsmClient;
}

}  // namespace Cellular
