#include <unity.h>
#include "config_runtime.h"

// In native builds, Config::getInt always returns the default (NVS stub).
// These tests verify the compile-time default fallback logic.

void setUp(void) {}
void tearDown(void) {}

void test_isEnabled_default_when_unset_ds18b20_returns_true() {
    // DEFAULT_FEAT_DS18B20 = 1 → should be enabled
    TEST_ASSERT_TRUE(Config::isEnabled("feat_ds18b20"));
}

void test_isEnabled_default_when_unset_scale_returns_false() {
    // DEFAULT_FEAT_SCALE = 0 → should be disabled
    TEST_ASSERT_FALSE(Config::isEnabled("feat_scale"));
}

void test_isEnabled_default_when_unset_sht31_returns_false() {
    // DEFAULT_FEAT_SHT31 = 0 → should be disabled
    TEST_ASSERT_FALSE(Config::isEnabled("feat_sht31"));
}

void test_isEnabled_default_when_unset_mic_returns_false() {
    // DEFAULT_FEAT_MIC = 0 → should be disabled
    TEST_ASSERT_FALSE(Config::isEnabled("feat_mic"));
}

void test_isEnabled_unknown_feature_returns_false() {
    // Unknown features default to 0 (off)
    TEST_ASSERT_FALSE(Config::isEnabled("feat_nonexistent"));
}

void test_getInt_default_when_unset() {
    // NVS stub always returns the supplied default
    TEST_ASSERT_EQUAL_INT32(42, Config::getInt("some_key", 42));
    TEST_ASSERT_EQUAL_INT32(0,  Config::getInt("some_key", 0));
    TEST_ASSERT_EQUAL_INT32(-1, Config::getInt("some_key", -1));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_isEnabled_default_when_unset_ds18b20_returns_true);
    RUN_TEST(test_isEnabled_default_when_unset_scale_returns_false);
    RUN_TEST(test_isEnabled_default_when_unset_sht31_returns_false);
    RUN_TEST(test_isEnabled_default_when_unset_mic_returns_false);
    RUN_TEST(test_isEnabled_unknown_feature_returns_false);
    RUN_TEST(test_getInt_default_when_unset);
    return UNITY_END();
}
