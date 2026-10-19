#include "state.h"

static SemaphoreHandle_t mutex;
static BmsSnapshot snap;

void stateInit() {
  mutex = xSemaphoreCreateMutex();
  memset(&snap, 0, sizeof(snap));
}

void stateSetBasic(const jbd::Basic& b) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  snap.basic = b;
  snap.valid = true;
  snap.updated_ms = millis();
  xSemaphoreGive(mutex);
}

void stateSetConnected(bool connected) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  snap.connected = connected;
  xSemaphoreGive(mutex);
}

BmsSnapshot stateGet() {
  xSemaphoreTake(mutex, portMAX_DELAY);
  BmsSnapshot copy = snap;
  xSemaphoreGive(mutex);
  return copy;
}
