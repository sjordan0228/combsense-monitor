#pragma once

#include <cstdint>

// =============================================================================
// Pin Definitions — defaults match XIAO ESP32-C6 silkscreen.
// Override at build time via -DPIN_I2C_SDA=N / -DPIN_I2C_SCL=N for other
// boards (Adafruit Feather S3, Freenove S3 Lite). Battery monitor uses
// the board-specific Arduino `A0` macro by default.
// =============================================================================

#ifndef PIN_I2C_SDA_GPIO
#define PIN_I2C_SDA_GPIO 4   // XIAO C6 D4
#endif
#ifndef PIN_I2C_SCL_GPIO
#define PIN_I2C_SCL_GPIO 5   // XIAO C6 D5
#endif

constexpr uint8_t PIN_I2C_SDA = PIN_I2C_SDA_GPIO;
constexpr uint8_t PIN_I2C_SCL = PIN_I2C_SCL_GPIO;

// =============================================================================
// BLE Advertisement
// =============================================================================

constexpr uint16_t MANUFACTURER_ID         = 0xFFFF;  // Prototyping
constexpr uint8_t  TAG_PROTOCOL_VERSION    = 0x01;
constexpr uint16_t DEFAULT_ADV_INTERVAL_SEC = 60;
constexpr uint16_t ADV_DURATION_MS         = 200;

// =============================================================================
// NVS
// =============================================================================

constexpr const char* NVS_NAMESPACE    = "combsense";
constexpr const char* NVS_KEY_TAG_NAME = "tag_name";
constexpr const char* NVS_KEY_ADV_INT  = "adv_interval";

// =============================================================================
// SHT31
// =============================================================================

constexpr uint8_t SHT31_ADDR = 0x44;
