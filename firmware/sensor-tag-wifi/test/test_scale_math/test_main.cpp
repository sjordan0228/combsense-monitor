#include <unity.h>
#include "scale_math.h"

void setUp(void) {}
void tearDown(void) {}

void test_stable_detector_empty_is_not_stable() {
    StableDetector d;
    TEST_ASSERT_FALSE(d.isStable());
}

void test_stable_detector_partial_fill_is_not_stable() {
    StableDetector d;
    d.push(1000);
    d.push(1010);
    TEST_ASSERT_FALSE(d.isStable());
}

void test_stable_detector_quiet_window_is_stable() {
    StableDetector d;
    for (int i = 0; i < 5; i++) d.push(1000);
    TEST_ASSERT_TRUE(d.isStable());
}

void test_stable_detector_within_tolerance_is_stable() {
    StableDetector d;
    d.push(1000);
    d.push(1020);
    d.push(1010);
    d.push(990);
    d.push(1015);  // range = 1020 - 990 = 30, < HX711_STABLE_TOLERANCE_RAW (50)
    TEST_ASSERT_TRUE(d.isStable());
}

void test_stable_detector_outside_tolerance_is_unstable() {
    StableDetector d;
    d.push(1000);
    d.push(1020);
    d.push(1010);
    d.push(990);
    d.push(1100);  // range = 1100 - 990 = 110, > 50
    TEST_ASSERT_FALSE(d.isStable());
}

void test_stable_detector_recovers_after_disturbance() {
    StableDetector d;
    for (int i = 0; i < 5; i++) d.push(1000);
    d.push(1500);  // one disturbance
    TEST_ASSERT_FALSE(d.isStable());
    d.push(1000);
    d.push(1000);
    d.push(1000);
    d.push(1000);  // ring buffer is now [1500, 1000, 1000, 1000, 1000] — range 500
    TEST_ASSERT_FALSE(d.isStable());
    d.push(1000);  // ring is now [1000, 1000, 1000, 1000, 1000] — stable
    TEST_ASSERT_TRUE(d.isStable());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_stable_detector_empty_is_not_stable);
    RUN_TEST(test_stable_detector_partial_fill_is_not_stable);
    RUN_TEST(test_stable_detector_quiet_window_is_stable);
    RUN_TEST(test_stable_detector_within_tolerance_is_stable);
    RUN_TEST(test_stable_detector_outside_tolerance_is_unstable);
    RUN_TEST(test_stable_detector_recovers_after_disturbance);
    return UNITY_END();
}
