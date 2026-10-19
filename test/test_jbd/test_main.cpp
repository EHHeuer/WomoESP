#include <unity.h>
#include <string.h>
#include "jbd.h"

using namespace jbd;

// Synthetischer 0x03-Rahmen nach dem Layout in CLAUDE.md (kein echter Mitschnitt):
// 13,28 V, -4,20 A, 120,00 Ah von 150,00 Ah, 12 Zyklen, SoC 80 %, 4 Zellen, 2 NTC (20,0 / 21,5 °C)
static size_t buildBasic(uint8_t* f, int16_t current_10mA) {
  uint8_t d[27] = {0};
  uint16_t c = (uint16_t)current_10mA;
  d[0] = 0x05; d[1] = 0x30;              // 1328
  d[2] = c >> 8; d[3] = c & 0xFF;
  d[4] = 0x2E; d[5] = 0xE0;              // 12000
  d[6] = 0x3A; d[7] = 0x98;              // 15000
  d[8] = 0x00; d[9] = 12;
  d[19] = 80; d[20] = 0x03; d[21] = 4; d[22] = 2;
  d[23] = 0x0B; d[24] = 0x73;            // 2931 -> 20,0 °C
  d[25] = 0x0B; d[26] = 0x82;            // 2946 -> 21,5 °C
  f[0] = 0xDD; f[1] = 0x03; f[2] = 0x00; f[3] = sizeof(d);
  memcpy(f + 4, d, sizeof(d));
  uint16_t sum = f[2] + f[3];
  for (uint8_t b : d) sum += b;
  uint16_t chk = (uint16_t)(0x10000 - sum);
  f[4 + sizeof(d)] = chk >> 8;
  f[5 + sizeof(d)] = chk & 0xFF;
  f[6 + sizeof(d)] = 0x77;
  return 7 + sizeof(d);
}

// Echter Mitschnitt vom eigenen BMS (DP04S007L4S150A), 19.10.2026, siehe test/fixtures/jbd_03_basic.hex
static const uint8_t REAL_BASIC[] = {
  0xDD, 0x03, 0x00, 0x22, 0x05, 0x4F, 0x00, 0xC8, 0x36, 0x72, 0x37, 0x0A, 0x00, 0x1C,
  0x33, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xD6, 0x63, 0x03, 0x04, 0x01, 0x0B,
  0x15, 0x00, 0x00, 0x00, 0x37, 0x0A, 0x36, 0x72, 0x00, 0x07, 0xFB, 0x24, 0x77};

void test_real_basic_frame() {
  Basic b;
  TEST_ASSERT_TRUE(parseBasic(REAL_BASIC, sizeof(REAL_BASIC), b));
  TEST_ASSERT_EQUAL_UINT16(1359, b.voltage_10mV);
  TEST_ASSERT_EQUAL_INT16(200, b.current_10mA);
  TEST_ASSERT_EQUAL_UINT16(13938, b.remain_10mAh);
  TEST_ASSERT_EQUAL_UINT16(14090, b.nominal_10mAh);
  TEST_ASSERT_EQUAL_UINT16(28, b.cycles);
  TEST_ASSERT_EQUAL_UINT16(0, b.protection);
  TEST_ASSERT_EQUAL_UINT8(99, b.soc);
  TEST_ASSERT_EQUAL_UINT8(4, b.cells);
  TEST_ASSERT_EQUAL_UINT8(1, b.ntc);
  TEST_ASSERT_EQUAL_INT16(106, b.temp_01C[0]);
  TEST_ASSERT_EQUAL_INT32(45, minutesToFull(b));
}

void test_command_checksums() {
  TEST_ASSERT_TRUE(verifyFrame(CMD_BASIC, sizeof(CMD_BASIC)));
  TEST_ASSERT_TRUE(verifyFrame(CMD_CELLS, sizeof(CMD_CELLS)));
}

void test_parse_basic() {
  uint8_t f[64];
  size_t n = buildBasic(f, -420);
  Basic b;
  TEST_ASSERT_TRUE(parseBasic(f, n, b));
  TEST_ASSERT_EQUAL_UINT16(1328, b.voltage_10mV);
  TEST_ASSERT_EQUAL_INT16(-420, b.current_10mA);
  TEST_ASSERT_EQUAL_UINT16(12000, b.remain_10mAh);
  TEST_ASSERT_EQUAL_UINT16(15000, b.nominal_10mAh);
  TEST_ASSERT_EQUAL_UINT16(12, b.cycles);
  TEST_ASSERT_EQUAL_UINT8(80, b.soc);
  TEST_ASSERT_EQUAL_UINT8(4, b.cells);
  TEST_ASSERT_EQUAL_UINT8(2, b.ntc);
  TEST_ASSERT_EQUAL_INT16(200, b.temp_01C[0]);
  TEST_ASSERT_EQUAL_INT16(215, b.temp_01C[1]);
}

void test_bad_checksum_rejected() {
  uint8_t f[64];
  size_t n = buildBasic(f, -420);
  f[10] ^= 0x01;
  Basic b;
  TEST_ASSERT_FALSE(parseBasic(f, n, b));
}

void test_assembler_split_notifications() {
  uint8_t f[64];
  size_t n = buildBasic(f, -420);
  FrameAssembler a;
  const uint8_t junk[] = {0x12, 0x34};
  TEST_ASSERT_FALSE(a.feed(junk, sizeof(junk)));
  TEST_ASSERT_FALSE(a.feed(f, 20));
  TEST_ASSERT_TRUE(a.feed(f + 20, n - 20));
  TEST_ASSERT_EQUAL(n, a.frameLen());
  Basic b;
  TEST_ASSERT_TRUE(parseBasic(a.frame(), a.frameLen(), b));
  TEST_ASSERT_EQUAL_UINT8(80, b.soc);
}

void test_runtime() {
  uint8_t f[64];
  Basic b;
  parseBasic(f, buildBasic(f, -420), b);
  TEST_ASSERT_EQUAL_INT32(1714, minutesToEmpty(b));  // 120 Ah / 4,2 A = 28,57 h
  TEST_ASSERT_EQUAL_INT32(-1, minutesToFull(b));

  parseBasic(f, buildBasic(f, 1000), b);
  TEST_ASSERT_EQUAL_INT32(-1, minutesToEmpty(b));
  TEST_ASSERT_EQUAL_INT32(180, minutesToFull(b));    // 30 Ah / 10 A = 3 h

  parseBasic(f, buildBasic(f, -5), b);
  TEST_ASSERT_EQUAL_INT32(-1, minutesToEmpty(b));
  TEST_ASSERT_EQUAL_INT32(-1, minutesToFull(b));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_command_checksums);
  RUN_TEST(test_real_basic_frame);
  RUN_TEST(test_parse_basic);
  RUN_TEST(test_bad_checksum_rejected);
  RUN_TEST(test_assembler_split_notifications);
  RUN_TEST(test_runtime);
  return UNITY_END();
}
