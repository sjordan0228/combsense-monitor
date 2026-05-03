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
    return UNITY_END();
}
