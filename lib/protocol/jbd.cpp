#include "jbd.h"

namespace jbd {

static uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

bool FrameAssembler::push(uint8_t b) {
  if (pos_ == 0 && b != 0xDD) return false;  // auf Rahmenstart synchronisieren
  if (pos_ >= MAX_FRAME) { reset(); return false; }
  buf_[pos_++] = b;
  if (pos_ < 4) return false;
  size_t expected = 4 + buf_[3] + 3;
  if (expected > MAX_FRAME) { reset(); return false; }
  if (pos_ < expected) return false;
  if (verifyFrame(buf_, pos_)) { done_ = true; return true; }
  reset();
  return false;
}

bool FrameAssembler::feed(const uint8_t* data, size_t len) {
  if (done_) reset();
  for (size_t i = 0; i < len; i++) {
    if (push(data[i])) return true;  // Rest der Notification verwerfen
  }
  return false;
}

bool verifyFrame(const uint8_t* f, size_t len) {
  if (len < 7 || f[0] != 0xDD || f[len - 1] != 0x77) return false;
  size_t dlen = f[3];
  if (len != 4 + dlen + 3) return false;
  uint16_t sum = 0;
  for (size_t i = 2; i < 4 + dlen; i++) sum += f[i];
  uint16_t chk = (uint16_t)(0x10000 - sum);
  return chk == be16(&f[4 + dlen]);
}

bool parseBasic(const uint8_t* f, size_t len, Basic& out) {
  if (!verifyFrame(f, len) || f[1] != 0x03 || f[2] != 0x00) return false;
  const uint8_t* d = f + 4;
  size_t dlen = f[3];
  if (dlen < 23) return false;
  out.voltage_10mV = be16(d + 0);
  out.current_10mA = (int16_t)be16(d + 2);
  out.remain_10mAh = be16(d + 4);
  out.nominal_10mAh = be16(d + 6);
  out.cycles = be16(d + 8);
  out.protection = be16(d + 16);
  out.soc = d[19];
  out.fet = d[20];
  out.cells = d[21];
  out.ntc = d[22];
  if (out.ntc > MAX_NTC || 23 + 2 * (size_t)out.ntc > dlen) return false;
  for (int i = 0; i < out.ntc; i++) out.temp_01C[i] = (int16_t)(be16(d + 23 + 2 * i) - 2731);
  return true;
}

int32_t minutesToEmpty(const Basic& b) {
  if (b.current_10mA > -IDLE_CURRENT_10mA) return -1;
  return (int32_t)b.remain_10mAh * 60 / -(int32_t)b.current_10mA;
}

int32_t minutesToFull(const Basic& b) {
  if (b.current_10mA < IDLE_CURRENT_10mA) return -1;
  int32_t missing = (int32_t)b.nominal_10mAh - b.remain_10mAh;
  if (missing < 0) missing = 0;
  return missing * 60 / b.current_10mA;
}

}  // namespace jbd
