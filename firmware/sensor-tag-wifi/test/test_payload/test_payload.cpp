#include <unity.h>
#include <cstring>
#include "reading.h"
#include "payload.h"

void setUp() {}
void tearDown() {}

void test_serialize_full_reading_with_humidity() {
    Reading r {
        .timestamp = 1712345678,
        .temp1 = 22.4f,
        .temp2 = 24.1f,
        .humidity1 = 52.3f,
        .humidity2 = 55.1f,
        .battery_pct = 87,
    };
    char buf[160];
    int n = Payload::serialize("ab12cd34", r, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING(
        "{\"id\":\"ab12cd34\",\"t\":1712345678,\"t1\":22.40,\"t2\":24.10,"
        "\"h1\":52.30,\"h2\":55.10,\"b\":87}",
        buf);
}

void test_serialize_ds18b20_reading_omits_humidity() {
    Reading r {
        .timestamp = 1712345678,
        .temp1 = 22.4f,
        .temp2 = 24.1f,
        .humidity1 = NAN,
        .humidity2 = NAN,
        .battery_pct = 87,
    };
    char buf[160];
    int n = Payload::serialize("ab12cd34", r, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING(
        "{\"id\":\"ab12cd34\",\"t\":1712345678,\"t1\":22.40,\"t2\":24.10,\"b\":87}",
        buf);
}

void test_serialize_returns_negative_on_undersized_buffer() {
    Reading r {
        .timestamp = 1712345678,
        .temp1 = 22.4f, .temp2 = 24.1f,
        .humidity1 = 52.3f, .humidity2 = 55.1f,
        .battery_pct = 87,
    };
    char buf[8];
    int n = Payload::serialize("ab12cd34", r, buf, sizeof(buf));
    TEST_ASSERT_LESS_THAN(0, n);
}

void test_serialize_emits_only_valid_humidity_channel() {
    Reading r {
        .timestamp = 1712345678,
        .temp1 = 22.4f,
        .temp2 = 24.1f,
        .humidity1 = 52.3f,
        .humidity2 = NAN,      // top SHT31 failed
        .battery_pct = 87,
    };
    char buf[160];
    int n = Payload::serialize("ab12cd34", r, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING(
        "{\"id\":\"ab12cd34\",\"t\":1712345678,\"t1\":22.40,\"t2\":24.10,"
        "\"h1\":52.30,\"b\":87}",
        buf);
}

void test_serialize_emits_null_for_nan_temps() {
    // Firmware must emit JSON `null` (not `nan`) so downstream JSON parsers
    // (Telegraf json_v2, Swift JSONDecoder, etc.) don't reject the message.
    Reading r {
        .timestamp = 1712345678,
        .temp1 = 21.50f,
        .temp2 = NAN,           // external DS18B20 not wired
        .humidity1 = NAN,
        .humidity2 = NAN,
        .battery_pct = 0,
    };
    char buf[160];
    int n = Payload::serialize("c5fffe12", r, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING(
        "{\"id\":\"c5fffe12\",\"t\":1712345678,\"t1\":21.50,\"t2\":null,\"b\":0}",
        buf);
}

void test_serialize_emits_t_zero_when_timestamp_unset() {
    // Readings buffered before the first NTP sync have timestamp=0. The payload
    // must serialize this as "t":0 so Telegraf can apply the t=0 fallback rule
    // and substitute the ingest time rather than silently emitting `nan` or
    // omitting the field.
    Reading r {
        .timestamp = 0,
        .temp1 = 20.00f,
        .temp2 = 21.00f,
        .humidity1 = NAN,
        .humidity2 = NAN,
        .battery_pct = 50,
    };
    char buf[160];
    int n = Payload::serialize("ab12cd34", r, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"t\":0,"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_serialize_full_reading_with_humidity);
    RUN_TEST(test_serialize_ds18b20_reading_omits_humidity);
    RUN_TEST(test_serialize_returns_negative_on_undersized_buffer);
    RUN_TEST(test_serialize_emits_only_valid_humidity_channel);
    RUN_TEST(test_serialize_emits_null_for_nan_temps);
    RUN_TEST(test_serialize_emits_t_zero_when_timestamp_unset);
    return UNITY_END();
}
