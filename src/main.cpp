// Phase 1: BLE-Scan, um das JBD-BMS zu finden. Wegwerf-Diagnose, wird in Phase 2 ersetzt.
#include <Arduino.h>
#include <NimBLEDevice.h>

static const char* KNOWN_MAC = "a5:c2:37:6e:f6:75";  // aus Vorgaenger-Firmware, unbestaetigt
static const uint32_t SCAN_MS = 10000;

class ScanCb : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* d) override {
    bool jbd = d->isAdvertisingService(NimBLEUUID((uint16_t)0xFF00));
    bool known = d->getAddress().toString() == KNOWN_MAC;
    Serial.printf("%s  RSSI %4d  %-20s %s%s\n",
      d->getAddress().toString().c_str(), d->getRSSI(),
      d->getName().c_str(), jbd ? "<-- JBD? " : "", known ? "<-- bekannte MAC" : "");
  }

  void onScanEnd(const NimBLEScanResults& results, int reason) override {
    Serial.printf("--- Scan fertig, %d Geraete ---\n", results.getCount());
  }
};

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("WomoESP: BLE-Scan");
  NimBLEDevice::init("");
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(new ScanCb());
  scan->setActiveScan(true);
}

void loop() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (!scan->isScanning()) {
    scan->clearResults();
    scan->start(SCAN_MS);  // NimBLE 2.x: ms, nicht blockierend
  }
  delay(500);
}
