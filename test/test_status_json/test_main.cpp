#include <unity.h>
#include <string.h>
#include "status_json.h"

// Echter Mitschnitt, siehe test/fixtures/jbd_03_basic.hex
static const uint8_t REAL_BASIC[] = {
  0xDD, 0x03, 0x00, 0x22, 0x05, 0x4F, 0x00, 0xC8, 0x36, 0x72, 0x37, 0x0A, 0x00, 0x1C,
  0x33, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xD6, 0x63, 0x03, 0x04, 0x01, 0x0B,
  0x15, 0x00, 0x00, 0x00, 0x37, 0x0A, 0x36, 0x72, 0x00, 0x07, 0xFB, 0x24, 0x77};

void test_no_data() {
  char buf[256];
  size_t n = model::statusJson(buf, sizeof(buf), nullptr, false, 0, 12);
  TEST_ASSERT_EQUAL_STRING("{\"uptime_s\":12,\"bms\":{\"connected\":false,\"valid\":false}}", buf);
  TEST_ASSERT_EQUAL(strlen(buf), n);
}

void test_real_frame() {
  jbd::Basic b;
  TEST_ASSERT_TRUE(jbd::parseBasic(REAL_BASIC, sizeof(REAL_BASIC), b));
  char buf[512];
  TEST_ASSERT_TRUE(model::statusJson(buf, sizeof(buf), &b, true, 3, 100) > 0);
  TEST_ASSERT_EQUAL_STRING(
    "{\"uptime_s\":100,\"bms\":{\"connected\":true,\"valid\":true,\"stale\":false,\"age_s\":3,"
    "\"soc\":99,\"voltage_v\":13.59,\"current_a\":2.00,\"remain_ah\":139.38,\"nominal_ah\":140.90,"
    "\"cycles\":28,\"protection\":0,\"minutes_to_empty\":null,\"minutes_to_full\":45,"
    "\"temp_c\":[10.6]}}", buf);
}

void test_stale_and_discharge() {
  jbd::Basic b;
  jbd::parseBasic(REAL_BASIC, sizeof(REAL_BASIC), b);
  b.current_10mA = -500;
  char buf[512];
  model::statusJson(buf, sizeof(buf), &b, false, model::STALE_AFTER_S, 100);
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"stale\":true"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"current_a\":-5.00"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"minutes_to_empty\":1672"));  // 139,38 Ah / 5 A
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"minutes_to_full\":null"));
}

void test_buffer_too_small() {
  jbd::Basic b;
  jbd::parseBasic(REAL_BASIC, sizeof(REAL_BASIC), b);
  char buf[40];
  TEST_ASSERT_EQUAL(0, model::statusJson(buf, sizeof(buf), &b, true, 0, 0));
  TEST_ASSERT_EQUAL_STRING("", buf);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_no_data);
  RUN_TEST(test_real_frame);
  RUN_TEST(test_stale_and_discharge);
  RUN_TEST(test_buffer_too_small);
  return UNITY_END();
}
