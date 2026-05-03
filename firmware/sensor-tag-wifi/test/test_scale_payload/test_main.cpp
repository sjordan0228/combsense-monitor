#include <unity.h>
#include <cstring>
#include <cmath>
#include "payload.h"
#include "reading.h"

void setUp(void) {}
void tearDown(void) {}

void test_payload_includes_w_when_finite() {
    Reading r{};
    r.timestamp = 1777759713;
    r.temp1     = 23.38f;
    r.temp2     = NAN;
    r.humidity1 = NAN;
    r.humidity2 = NAN;
    r.vbat_mV   = 3505;
    r.battery_pct = 22;
    r.weight_kg = 47.32f;

    char buf[256];
    int n = Payload::serialize("c513131c", "abc1234", -87, r, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"w\":47.32"));
}

void test_payload_omits_w_when_nan() {
    Reading r{};
    r.timestamp = 1777759713;
    r.temp1     = 23.38f;
    r.temp2     = NAN;
    r.humidity1 = NAN;
    r.humidity2 = NAN;
    r.vbat_mV   = 3505;
    r.battery_pct = 22;
    r.weight_kg = NAN;

    char buf[256];
    Payload::serialize("c513131c", "abc1234", -87, r, buf, sizeof(buf));
    TEST_ASSERT_NULL(strstr(buf, "\"w\""));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_payload_includes_w_when_finite);
    RUN_TEST(test_payload_omits_w_when_nan);
    return UNITY_END();
}
