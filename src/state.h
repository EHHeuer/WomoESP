// Zentrales Datenmodell hinter einem Mutex. Schreibt: bms_task. Liest: web.
#pragma once
#include <Arduino.h>
#include "jbd.h"

struct BmsSnapshot {
  jbd::Basic basic;
  bool valid;          // mindestens einmal gueltige Daten empfangen
  bool connected;
  uint32_t updated_ms; // millis() der letzten gueltigen Aktualisierung
};

void stateInit();
void stateSetBasic(const jbd::Basic& b);
void stateSetConnected(bool connected);
BmsSnapshot stateGet();
