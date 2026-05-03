#include <unity.h>
#include <cstring>
#include "scale_commands.h"

void setUp(void) {}
void tearDown(void) {}

void test_parse_tare() {
    ScaleCommand cmd;
    bool ok = parseScaleCommand(R"({"cmd":"tare"})", cmd);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(ScaleCommandType::Tare, cmd.type);
}

void test_parse_calibrate() {
    ScaleCommand cmd;
    bool ok = parseScaleCommand(R"({"cmd":"calibrate","known_kg":10})", cmd);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(ScaleCommandType::Calibrate, cmd.type);
    TEST_ASSERT_EQUAL_DOUBLE(10.0, cmd.calibrate.known_kg);
}

void test_parse_verify() {
    ScaleCommand cmd;
    bool ok = parseScaleCommand(R"({"cmd":"verify","expected_kg":5.0})", cmd);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(ScaleCommandType::Verify, cmd.type);
    TEST_ASSERT_EQUAL_DOUBLE(5.0, cmd.verify.expected_kg);
}

void test_parse_stream_raw() {
    ScaleCommand cmd;
    bool ok = parseScaleCommand(R"({"cmd":"stream_raw","duration_sec":90})", cmd);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(ScaleCommandType::StreamRaw, cmd.type);
    TEST_ASSERT_EQUAL(90, cmd.stream_raw.duration_sec);
}

void test_parse_stop_stream() {
    ScaleCommand cmd;
    bool ok = parseScaleCommand(R"({"cmd":"stop_stream"})", cmd);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(ScaleCommandType::StopStream, cmd.type);
}

void test_parse_modify_start() {
    ScaleCommand cmd;
    bool ok = parseScaleCommand(R"({"cmd":"modify_start","label":"added_super_deep"})", cmd);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(ScaleCommandType::ModifyStart, cmd.type);
    TEST_ASSERT_EQUAL_STRING("added_super_deep", cmd.modify.label);
}

void test_parse_modify_end() {
    ScaleCommand cmd;
    bool ok = parseScaleCommand(R"({"cmd":"modify_end","label":"extracted_honey"})", cmd);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(ScaleCommandType::ModifyEnd, cmd.type);
    TEST_ASSERT_EQUAL_STRING("extracted_honey", cmd.modify.label);
}

void test_parse_modify_cancel() {
    ScaleCommand cmd;
    bool ok = parseScaleCommand(R"({"cmd":"modify_cancel"})", cmd);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(ScaleCommandType::ModifyCancel, cmd.type);
}

void test_parse_unknown_cmd_fails() {
    ScaleCommand cmd;
    bool ok = parseScaleCommand(R"({"cmd":"frobnicate"})", cmd);
    TEST_ASSERT_FALSE(ok);
}

void test_parse_missing_required_field_fails() {
    ScaleCommand cmd;
    bool ok = parseScaleCommand(R"({"cmd":"calibrate"})", cmd);  // missing known_kg
    TEST_ASSERT_FALSE(ok);
}

void test_parse_invalid_json_fails() {
    ScaleCommand cmd;
    bool ok = parseScaleCommand("not json", cmd);
    TEST_ASSERT_FALSE(ok);
}

void test_serialize_awake() {
    char buf[256];
    int n = serializeAwakeEvent(1777759800, 1777759714, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"event\":\"awake\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"keep_alive_until\":\"2026-05-02T22:10:00Z\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ts\":\"2026-05-02T22:08:34Z\""));
}

void test_serialize_tare_saved() {
    char buf[256];
    serializeTareSavedEvent(1234567, 1777759714, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"event\":\"tare_saved\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"raw_offset\":1234567"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ts\":\"2026-05-02T22:08:34Z\""));
}

void test_serialize_calibration_saved() {
    char buf[256];
    serializeCalibrationSavedEvent(4567.89, 1.8, 1777759714, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"event\":\"calibration_saved\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"scale_factor\":4567.89"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"predicted_accuracy_pct\":1.8"));
}

void test_serialize_verify_result() {
    char buf[256];
    serializeVerifyResultEvent(4.97, 5.0, 0.6, 1777759714, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"event\":\"verify_result\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"measured_kg\":4.97"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"expected_kg\":5"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"error_pct\":0.6"));
}

void test_serialize_raw_stream() {
    char buf[256];
    serializeRawStreamEvent(5678901, 47.32, true, 1777759714, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"event\":\"raw_stream\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"raw_value\":5678901"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"kg\":47.32"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"stable\":true"));
}

void test_serialize_modify_complete() {
    char buf[256];
    serializeModifyCompleteEvent("added_super_deep", 47.3, 58.1, 10.8, 287, false,
                                 1777759714, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"event\":\"modify_complete\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"label\":\"added_super_deep\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"delta_kg\":10.8"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"tare_updated\":false"));
}

void test_serialize_modify_warning() {
    char buf[256];
    serializeModifyWarningEvent("inspection_only", 0.2, "no_significant_change_detected",
                                1777759714, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"event\":\"modify_warning\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"warning\":\"no_significant_change_detected\""));
}

void test_serialize_error() {
    char buf[256];
    serializeErrorEvent("hx711_unresponsive", "no DOUT pulse for 1s", 1777759714, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"event\":\"error\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"code\":\"hx711_unresponsive\""));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_tare);
    RUN_TEST(test_parse_calibrate);
    RUN_TEST(test_parse_verify);
    RUN_TEST(test_parse_stream_raw);
    RUN_TEST(test_parse_stop_stream);
    RUN_TEST(test_parse_modify_start);
    RUN_TEST(test_parse_modify_end);
    RUN_TEST(test_parse_modify_cancel);
    RUN_TEST(test_parse_unknown_cmd_fails);
    RUN_TEST(test_parse_missing_required_field_fails);
    RUN_TEST(test_parse_invalid_json_fails);
    RUN_TEST(test_serialize_awake);
    RUN_TEST(test_serialize_tare_saved);
    RUN_TEST(test_serialize_calibration_saved);
    RUN_TEST(test_serialize_verify_result);
    RUN_TEST(test_serialize_raw_stream);
    RUN_TEST(test_serialize_modify_complete);
    RUN_TEST(test_serialize_modify_warning);
    RUN_TEST(test_serialize_error);
    return UNITY_END();
}
