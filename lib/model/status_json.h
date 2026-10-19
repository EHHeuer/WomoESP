// JSON fuer GET /api/status. Hardwareunabhaengig, nativ getestet.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "jbd.h"

namespace model {

// Ab diesem Alter gelten die BMS-Werte als veraltet.
static const uint32_t STALE_AFTER_S = 30;

// basic == nullptr: noch nie gueltige Daten empfangen.
// Liefert die Laenge ohne Nullterminator oder 0, wenn der Puffer zu klein ist.
size_t statusJson(char* buf, size_t size, const jbd::Basic* basic, bool connected,
                  uint32_t age_s, uint32_t uptime_s);

}  // namespace model
