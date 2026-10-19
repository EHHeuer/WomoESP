#include "status_json.h"
#include <stdarg.h>
#include <stdio.h>

namespace model {

namespace {
struct Writer {
  char* buf;
  size_t size;
  size_t pos = 0;
  bool overflow = false;

  Writer(char* b, size_t s) : buf(b), size(s) {}

  void add(const char* fmt, ...) {
    if (overflow) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + pos, size - pos, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= size - pos) { overflow = true; return; }
    pos += n;
  }

  void minutes(const char* key, int32_t m) {
    if (m < 0) add(",\"%s\":null", key);
    else add(",\"%s\":%ld", key, (long)m);
  }
};
}  // namespace

size_t statusJson(char* buf, size_t size, const jbd::Basic* b, bool connected,
                  uint32_t age_s, uint32_t uptime_s) {
  if (size == 0) return 0;
  Writer w(buf, size);
  w.add("{\"uptime_s\":%lu,\"bms\":{\"connected\":%s", (unsigned long)uptime_s,
        connected ? "true" : "false");
  if (!b) {
    w.add(",\"valid\":false}}");
  } else {
    w.add(",\"valid\":true,\"stale\":%s,\"age_s\":%lu",
          age_s >= STALE_AFTER_S ? "true" : "false", (unsigned long)age_s);
    w.add(",\"soc\":%u,\"voltage_v\":%.2f,\"current_a\":%.2f,\"remain_ah\":%.2f,\"nominal_ah\":%.2f",
          b->soc, b->voltage_10mV / 100.0, b->current_10mA / 100.0,
          b->remain_10mAh / 100.0, b->nominal_10mAh / 100.0);
    w.add(",\"cycles\":%u,\"protection\":%u", b->cycles, b->protection);
    w.minutes("minutes_to_empty", jbd::minutesToEmpty(*b));
    w.minutes("minutes_to_full", jbd::minutesToFull(*b));
    w.add(",\"temp_c\":[");
    for (int i = 0; i < b->ntc; i++) w.add(i ? ",%.1f" : "%.1f", b->temp_01C[i] / 10.0);
    w.add("]}}");
  }
  if (w.overflow) { buf[0] = '\0'; return 0; }
  return w.pos;
}

}  // namespace model
