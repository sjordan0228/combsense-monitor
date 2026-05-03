#include <unity.h>
#include <cstring>
#include "config_ack.h"
#include "config_parser.h"

void setUp(void) {}
void tearDown(void) {}

// --- AckEntry struct shape ---------------------------------------------------

void test_ack_entry_key_size() {
    // key must hold at least a 15-char NVS key name + NUL.
    AckEntry e {};
    TEST_ASSERT_EQUAL_size_t(16u, sizeof(e.key));
}

void test_ack_entry_result_size() {
    // result must hold longest expected string "excluded:security" + NUL (≤32).
    AckEntry e {};
    TEST_ASSERT_EQUAL_size_t(32u, sizeof(e.result));
}

void test_ack_entry_total_size() {
    // Struct is 48 bytes (16 + 32); no padding expected between char arrays.
    TEST_ASSERT_EQUAL_size_t(48u, sizeof(AckEntry));
}

void test_ack_entry_fields_writable() {
    AckEntry e {};
    strncpy(e.key,    "sample_int",  sizeof(e.key)    - 1);
    strncpy(e.result, "ok",          sizeof(e.result) - 1);
    TEST_ASSERT_EQUAL_STRING("sample_int", e.key);
    TEST_ASSERT_EQUAL_STRING("ok",         e.result);
}

// --- preValidate — no-conflict paths -----------------------------------------

void test_preValidate_returns_true_empty_parsed() {
    ConfigParser::ConfigUpdate parsed {};
    parsed.feat_ds18b20 = ConfigParser::FeatFlag::Absent;
    parsed.feat_sht31   = ConfigParser::FeatFlag::Absent;
    parsed.feat_scale   = ConfigParser::FeatFlag::Absent;
    parsed.feat_mic     = ConfigParser::FeatFlag::Absent;
    TemperatureNvsState nvs { false, false };
    AckEntry entries[8];
    size_t count = 99;  // sentinel to confirm it's zeroed
    bool ok = preValidate(parsed, nvs, entries, &count);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(0u, count);
}

void test_preValidate_returns_true_full_parsed_no_feat() {
    // Non-feat keys present; no feat conflict.
    ConfigParser::ConfigUpdate parsed {};
    parsed.feat_ds18b20 = ConfigParser::FeatFlag::Absent;
    parsed.feat_sht31   = ConfigParser::FeatFlag::Absent;
    parsed.feat_scale   = ConfigParser::FeatFlag::Absent;
    parsed.feat_mic     = ConfigParser::FeatFlag::Absent;
    parsed.has_sample_int   = true;  parsed.sample_int   = 300;
    parsed.has_upload_every = true;  parsed.upload_every = 2;
    parsed.has_tag_name     = true;  strncpy(parsed.tag_name, "hive-1", sizeof(parsed.tag_name) - 1);
    parsed.has_ota_host     = true;  strncpy(parsed.ota_host, "192.168.1.61", sizeof(parsed.ota_host) - 1);
    TemperatureNvsState nvs { true, false };  // ds18b20 on, sht31 off

    AckEntry entries[8];
    size_t count = 99;
    bool ok = preValidate(parsed, nvs, entries, &count);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(0u, count);
}

void test_preValidate_null_out_count_safe() {
    // Must not crash when outCount is null (caller may omit it).
    ConfigParser::ConfigUpdate parsed {};
    parsed.feat_ds18b20 = ConfigParser::FeatFlag::Absent;
    parsed.feat_sht31   = ConfigParser::FeatFlag::Absent;
    parsed.feat_scale   = ConfigParser::FeatFlag::Absent;
    parsed.feat_mic     = ConfigParser::FeatFlag::Absent;
    TemperatureNvsState nvs { false, false };
    AckEntry entries[8];
    bool ok = preValidate(parsed, nvs, entries, nullptr);
    TEST_ASSERT_TRUE(ok);
}

// --- preValidate — mutual-exclusion conflict tests ---------------------------

void test_preValidate_rejects_both_temp_sensors_enabled() {
    // parsed has both at 1, NVS empty (both off) → post-apply both on → conflict
    ConfigParser::ConfigUpdate parsed {};
    parsed.feat_ds18b20 = ConfigParser::FeatFlag::On;
    parsed.feat_sht31   = ConfigParser::FeatFlag::On;
    parsed.feat_scale   = ConfigParser::FeatFlag::Absent;
    parsed.feat_mic     = ConfigParser::FeatFlag::Absent;
    TemperatureNvsState nvs { false, false };
    AckEntry entries[8];
    size_t count = 0;
    bool ok = preValidate(parsed, nvs, entries, &count);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_size_t(2u, count);
    // Both keys must appear in entries
    bool foundSht31 = false, foundDs = false;
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(entries[i].key, "feat_sht31")   == 0) foundSht31 = true;
        if (strcmp(entries[i].key, "feat_ds18b20") == 0) foundDs    = true;
    }
    TEST_ASSERT_TRUE(foundSht31);
    TEST_ASSERT_TRUE(foundDs);
    // Check the conflict results point at each other
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(entries[i].key, "feat_sht31") == 0) {
            TEST_ASSERT_EQUAL_STRING("conflict:feat_ds18b20", entries[i].result);
        }
        if (strcmp(entries[i].key, "feat_ds18b20") == 0) {
            TEST_ASSERT_EQUAL_STRING("conflict:feat_sht31", entries[i].result);
        }
    }
}

void test_preValidate_allows_disabling_both_temp() {
    // Both 0 is legal (no active temp sensor is a valid config).
    ConfigParser::ConfigUpdate parsed {};
    parsed.feat_ds18b20 = ConfigParser::FeatFlag::Off;
    parsed.feat_sht31   = ConfigParser::FeatFlag::Off;
    parsed.feat_scale   = ConfigParser::FeatFlag::Absent;
    parsed.feat_mic     = ConfigParser::FeatFlag::Absent;
    TemperatureNvsState nvs { true, false };
    AckEntry entries[8];
    size_t count = 0;
    bool ok = preValidate(parsed, nvs, entries, &count);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(0u, count);
}

void test_preValidate_allows_swap_via_two_keys() {
    // Swap: parsed ds18b20=0, sht31=1; NVS ds18b20=1 → post-apply ds18b20=0, sht31=1 → OK
    ConfigParser::ConfigUpdate parsed {};
    parsed.feat_ds18b20 = ConfigParser::FeatFlag::Off;
    parsed.feat_sht31   = ConfigParser::FeatFlag::On;
    parsed.feat_scale   = ConfigParser::FeatFlag::Absent;
    parsed.feat_mic     = ConfigParser::FeatFlag::Absent;
    TemperatureNvsState nvs { true, false };
    AckEntry entries[8];
    size_t count = 0;
    bool ok = preValidate(parsed, nvs, entries, &count);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(0u, count);
}

void test_preValidate_rejects_one_key_against_existing() {
    // Parsed has only sht31=1; NVS ds18b20=1 → post-apply both on → conflict
    ConfigParser::ConfigUpdate parsed {};
    parsed.feat_ds18b20 = ConfigParser::FeatFlag::Absent;
    parsed.feat_sht31   = ConfigParser::FeatFlag::On;
    parsed.feat_scale   = ConfigParser::FeatFlag::Absent;
    parsed.feat_mic     = ConfigParser::FeatFlag::Absent;
    TemperatureNvsState nvs { true, false };
    AckEntry entries[8];
    size_t count = 0;
    bool ok = preValidate(parsed, nvs, entries, &count);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_size_t(2u, count);
}

// --- Unity runner ------------------------------------------------------------

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_ack_entry_key_size);
    RUN_TEST(test_ack_entry_result_size);
    RUN_TEST(test_ack_entry_total_size);
    RUN_TEST(test_ack_entry_fields_writable);
    RUN_TEST(test_preValidate_returns_true_empty_parsed);
    RUN_TEST(test_preValidate_returns_true_full_parsed_no_feat);
    RUN_TEST(test_preValidate_null_out_count_safe);
    RUN_TEST(test_preValidate_rejects_both_temp_sensors_enabled);
    RUN_TEST(test_preValidate_allows_disabling_both_temp);
    RUN_TEST(test_preValidate_allows_swap_via_two_keys);
    RUN_TEST(test_preValidate_rejects_one_key_against_existing);
    return UNITY_END();
}
