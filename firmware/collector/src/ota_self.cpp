#include "ota_self.h"
#include "config.h"
#include "cellular.h"
#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_http_client.h>
#include <rom/crc.h>

namespace OtaSelf {

bool downloadAndApply(const char* tag) {
    // Build URL: <GITHUB_RELEASE_BASE><tag>/collector.bin
    char url[256];
    snprintf(url, sizeof(url), "%s%s/collector.bin", GITHUB_RELEASE_BASE, tag);
    Serial.printf("[OTA-SELF] Starting self-OTA from: %s\n", url);

    // Get the next OTA partition
    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
    if (!update_partition) {
        Serial.printf("[OTA-SELF] ERROR: No OTA update partition found\n");
        return false;
    }
    Serial.printf("[OTA-SELF] Writing to partition: %s\n", update_partition->label);

    // Begin OTA
    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        Serial.printf("[OTA-SELF] ERROR: esp_ota_begin failed: %s\n", esp_err_to_name(err));
        return false;
    }

    // Configure HTTP client
    esp_http_client_config_t http_config = {};
    http_config.url        = url;
    http_config.timeout_ms = 30000;

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (!client) {
        Serial.printf("[OTA-SELF] ERROR: Failed to init HTTP client\n");
        esp_ota_abort(ota_handle);
        return false;
    }

    // Open connection and fetch headers
    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        Serial.printf("[OTA-SELF] ERROR: esp_http_client_open failed: %s\n", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        esp_ota_abort(ota_handle);
        return false;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
        Serial.printf("[OTA-SELF] WARNING: Content-Length unknown, proceeding anyway\n");
    } else {
        Serial.printf("[OTA-SELF] Firmware size: %d bytes\n", content_length);
    }

    // Read loop — 1024-byte buffer
    uint8_t buf[1024];
    int total_bytes = 0;
    uint32_t running_crc = 0;
    bool read_error = false;

    while (true) {
        int bytes_read = esp_http_client_read(client, (char*)buf, sizeof(buf));
        if (bytes_read < 0) {
            Serial.printf("[OTA-SELF] ERROR: HTTP read failed at offset %d\n", total_bytes);
            read_error = true;
            break;
        }
        if (bytes_read == 0) {
            // End of stream
            break;
        }

        err = esp_ota_write(ota_handle, buf, bytes_read);
        if (err != ESP_OK) {
            Serial.printf("[OTA-SELF] ERROR: esp_ota_write failed: %s\n", esp_err_to_name(err));
            read_error = true;
            break;
        }

        running_crc = crc32_le(running_crc, buf, bytes_read);
        total_bytes += bytes_read;
    }

    // Cleanup HTTP client regardless of outcome
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (read_error) {
        esp_ota_abort(ota_handle);
        return false;
    }

    Serial.printf("[OTA-SELF] Download complete — %d bytes, CRC32=0x%08X\n", total_bytes, running_crc);

    // Finalise OTA
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        Serial.printf("[OTA-SELF] ERROR: esp_ota_end failed: %s\n", esp_err_to_name(err));
        return false;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        Serial.printf("[OTA-SELF] ERROR: esp_ota_set_boot_partition failed: %s\n", esp_err_to_name(err));
        return false;
    }

    Serial.printf("[OTA-SELF] Boot partition set. Rebooting into new firmware...\n");
    esp_restart();

    // Unreachable — satisfies compiler
    return true;
}

void validateNewFirmware() {
    // Get running partition and check its OTA state
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    esp_err_t err = esp_ota_get_state_partition(running, &ota_state);

    if (err != ESP_OK || ota_state != ESP_OTA_IMG_PENDING_VERIFY) {
        // Not an OTA boot — nothing to validate
        return;
    }

    Serial.printf("[OTA-SELF] New firmware pending verify — running health check\n");

    // Health check: try to bring the cellular modem up
    bool healthy = Cellular::powerOn();

    if (healthy) {
        Serial.printf("[OTA-SELF] Health check PASSED — marking firmware valid\n");
        esp_ota_mark_app_valid_cancel_rollback();
        Cellular::powerOff();
    } else {
        Serial.printf("[OTA-SELF] Health check FAILED — rolling back to previous firmware\n");
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}

} // namespace OtaSelf
