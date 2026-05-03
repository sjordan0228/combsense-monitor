/// Native tests for config/get → config/state policy logic.
///
/// Tests exercise the `isConfigGetExcluded` policy function and the key-set
/// semantics — the NVS read path is Arduino-only and cannot run host-side.
/// A mock-NVS helper below verifies the JSON building logic independently.

#include <unity.h>
#include <cstring>
#include <ArduinoJson.h>
#include "config_ack.h"

void setUp(void) {}
void tearDown(void) {}

// --- Exclusion policy --------------------------------------------------------

void test_handleConfigGet_excludes_wifi_pass_even_if_explicitly_requested() {
    // wifi_pass is excluded by policy — isConfigGetExcluded must return true.
    TEST_ASSERT_TRUE(isConfigGetExcluded("wifi_pass"));
}

void test_handleConfigGet_excludes_mqtt_pass() {
    TEST_ASSERT_TRUE(isConfigGetExcluded("mqtt_pass"));
}

void test_handleConfigGet_allows_known_config_keys() {
    // Known non-excluded keys must NOT be excluded.
    TEST_ASSERT_FALSE(isConfigGetExcluded("feat_ds18b20"));
    TEST_ASSERT_FALSE(isConfigGetExcluded("feat_sht31"));
    TEST_ASSERT_FALSE(isConfigGetExcluded("feat_scale"));
    TEST_ASSERT_FALSE(isConfigGetExcluded("feat_mic"));
    TEST_ASSERT_FALSE(isConfigGetExcluded("sample_int"));
    TEST_ASSERT_FALSE(isConfigGetExcluded("upload_every"));
    TEST_ASSERT_FALSE(isConfigGetExcluded("tag_name"));
    TEST_ASSERT_FALSE(isConfigGetExcluded("ota_host"));
}

// --- Mock-NVS JSON building --------------------------------------------------
//
// Simulate the handleConfigGet response-build logic using mock values so we
// can verify the JSON structure without touching NVS.

static void buildMockConfigStateJson(
        const char* requestedKeysJson,   // null or '{"keys":[...]}'
        char* out, size_t outCap) {
    // Known mock NVS values
    struct MockEntry { const char* key; const char* value; } mockNvs[] = {
        { "feat_ds18b20",  "1" },
        { "feat_sht31",    "0" },
        { "feat_scale",    "0" },
        { "feat_mic",      "0" },
        { "sample_int",    "300" },
        { "upload_every",  "1" },
        { "tag_name",      "yard" },
        { "ota_host",      "192.168.1.61" },
    };
    constexpr size_t NUM = sizeof(mockNvs) / sizeof(mockNvs[0]);

    // Parse requested keys filter
    bool includeAll = true;
    char requestedKeys[8][16] = {};
    size_t numRequested = 0;

    if (requestedKeysJson && strlen(requestedKeysJson) > 0) {
        JsonDocument req;
        if (deserializeJson(req, requestedKeysJson) == DeserializationError::Ok) {
            JsonArray arr = req["keys"].as<JsonArray>();
            if (!arr.isNull()) {
                includeAll = false;
                for (JsonVariant v : arr) {
                    const char* k = v.as<const char*>();
                    if (k && numRequested < 8) {
                        strncpy(requestedKeys[numRequested], k, 15);
                        requestedKeys[numRequested][15] = '\0';
                        numRequested++;
                    }
                }
            }
        }
    }

    auto shouldInclude = [&](const char* name) -> bool {
        if (isConfigGetExcluded(name)) return false;
        if (includeAll) return true;
        for (size_t i = 0; i < numRequested; ++i) {
            if (strcmp(requestedKeys[i], name) == 0) return true;
        }
        return false;
    };

    JsonDocument doc;
    doc["event"] = "config_state";
    JsonObject values = doc["values"].to<JsonObject>();
    for (size_t i = 0; i < NUM; ++i) {
        if (shouldInclude(mockNvs[i].key)) {
            values[mockNvs[i].key] = mockNvs[i].value;
        }
    }
    doc["ts"] = "1970-01-01T00:00:00Z";

    serializeJson(doc, out, outCap);
}

void test_handleConfigGet_returns_all_keys_for_empty_request() {
    char buf[512];
    buildMockConfigStateJson(nullptr, buf, sizeof(buf));

    JsonDocument doc;
    auto err = deserializeJson(doc, buf);
    TEST_ASSERT_EQUAL_INT(0, (int)err.code());

    TEST_ASSERT_EQUAL_STRING("config_state", doc["event"].as<const char*>());
    // All 8 known keys should be present
    TEST_ASSERT_FALSE(doc["values"]["feat_ds18b20"].isNull());
    TEST_ASSERT_FALSE(doc["values"]["feat_sht31"].isNull());
    TEST_ASSERT_FALSE(doc["values"]["feat_scale"].isNull());
    TEST_ASSERT_FALSE(doc["values"]["feat_mic"].isNull());
    TEST_ASSERT_FALSE(doc["values"]["sample_int"].isNull());
    TEST_ASSERT_FALSE(doc["values"]["upload_every"].isNull());
    TEST_ASSERT_FALSE(doc["values"]["tag_name"].isNull());
    TEST_ASSERT_FALSE(doc["values"]["ota_host"].isNull());
    // Excluded keys must not appear
    TEST_ASSERT_TRUE(doc["values"]["wifi_pass"].isNull());
    TEST_ASSERT_TRUE(doc["values"]["mqtt_pass"].isNull());
}

void test_handleConfigGet_excludes_wifi_pass_when_explicitly_requested() {
    // Even if wifi_pass appears in the keys filter, it must not be returned.
    char buf[512];
    buildMockConfigStateJson("{\"keys\":[\"sample_int\",\"wifi_pass\",\"feat_scale\"]}", buf, sizeof(buf));

    JsonDocument doc;
    deserializeJson(doc, buf);
    TEST_ASSERT_FALSE(doc["values"]["sample_int"].isNull());
    TEST_ASSERT_FALSE(doc["values"]["feat_scale"].isNull());
    TEST_ASSERT_TRUE(doc["values"]["wifi_pass"].isNull());   // excluded
}

void test_handleConfigGet_filters_to_requested_subset() {
    char buf[512];
    buildMockConfigStateJson("{\"keys\":[\"sample_int\",\"feat_scale\"]}", buf, sizeof(buf));

    JsonDocument doc;
    deserializeJson(doc, buf);
    TEST_ASSERT_FALSE(doc["values"]["sample_int"].isNull());
    TEST_ASSERT_FALSE(doc["values"]["feat_scale"].isNull());
    // Keys not in the filter must be absent
    TEST_ASSERT_TRUE(doc["values"]["tag_name"].isNull());
    TEST_ASSERT_TRUE(doc["values"]["upload_every"].isNull());
    TEST_ASSERT_TRUE(doc["values"]["feat_ds18b20"].isNull());
}

// --- Unity runner ------------------------------------------------------------

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_handleConfigGet_excludes_wifi_pass_even_if_explicitly_requested);
    RUN_TEST(test_handleConfigGet_excludes_mqtt_pass);
    RUN_TEST(test_handleConfigGet_allows_known_config_keys);
    RUN_TEST(test_handleConfigGet_returns_all_keys_for_empty_request);
    RUN_TEST(test_handleConfigGet_excludes_wifi_pass_when_explicitly_requested);
    RUN_TEST(test_handleConfigGet_filters_to_requested_subset);
    return UNITY_END();
}
