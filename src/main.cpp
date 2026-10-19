// JBD per BLE auslesen, SoC und Restlaufzeit per Serial, REST-API und E-Ink-Simulation im WLAN.
#include <Arduino.h>
#include <ESPmDNS.h>
#include <NimBLEDevice.h>
#include <WebServer.h>
#include <WiFi.h>
#include "jbd.h"
#include "secrets.h"
#include "state.h"
#include "status_json.h"
#include "web_ui.h"

static const char* HOSTNAME = "batterie";
static const char* BMS_MAC = "a5:c2:37:6e:f6:75";
static const uint32_t POLL_MS = 5000;
static const uint32_t RESPONSE_TIMEOUT_MS = 2000;
static const uint32_t SCAN_MS = 5000;
static const uint32_t BMS_TASK_STACK = 6144;

static const NimBLEUUID SVC_UUID((uint16_t)0xFF00);
static const NimBLEUUID NOTIFY_UUID((uint16_t)0xFF01);
static const NimBLEUUID WRITE_UUID((uint16_t)0xFF02);

struct Chunk {
  uint8_t len;
  uint8_t data[64];
};
static QueueHandle_t chunkQueue;

// BLE-Callback: nur kopieren, keine Allokation
static void onNotify(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
  Chunk c;
  c.len = len > sizeof(c.data) ? sizeof(c.data) : len;
  memcpy(c.data, data, c.len);
  xQueueSend(chunkQueue, &c, 0);
}

static void printHex(const uint8_t* d, size_t n) {
  for (size_t i = 0; i < n; i++) Serial.printf("%02X ", d[i]);
  Serial.println();
}

static void printDuration(int32_t min) {
  Serial.printf("%ld h %02ld min", (long)(min / 60), (long)(min % 60));
}

static void printBasic(const jbd::Basic& b) {
  Serial.printf("SoC %u %%  %.2f/%.2f Ah  %.2f V  %+.2f A  ",
    b.soc, b.remain_10mAh / 100.0, b.nominal_10mAh / 100.0,
    b.voltage_10mV / 100.0, b.current_10mA / 100.0);
  int32_t empty = jbd::minutesToEmpty(b);
  int32_t full = jbd::minutesToFull(b);
  if (empty >= 0) { Serial.print("leer in "); printDuration(empty); }
  else if (full >= 0) { Serial.print("voll in "); printDuration(full); }
  else Serial.print("kein nennenswerter Strom");
  Serial.println();
}

static bool connectBms(NimBLEClient* client, NimBLERemoteCharacteristic*& writeChr) {
  // Erst suchen: verbindet mit dem richtigen Adresstyp und zeigt, ob das BMS ueberhaupt wirbt
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(false);
  NimBLEScanResults results = scan->getResults(SCAN_MS);
  const NimBLEAdvertisedDevice* dev = nullptr;
  for (int i = 0; i < results.getCount(); i++) {
    if (results.getDevice(i)->getAddress().toString() == BMS_MAC) dev = results.getDevice(i);
  }
  if (!dev) {
    Serial.printf("[BMS] %s wirbt nicht (ausser Reichweite oder anderes Geraet verbunden)\n", BMS_MAC);
    return false;
  }
  Serial.printf("[BMS] Gefunden, RSSI %d, Adresstyp %u. Verbinde ...\n",
    dev->getRSSI(), dev->getAddress().getType());
  bool connected = client->connect(dev);
  scan->clearResults();
  if (!connected) {
    Serial.printf("[BMS] Verbindung fehlgeschlagen, Fehler %d\n", client->getLastError());
    return false;
  }
  NimBLERemoteService* svc = client->getService(SVC_UUID);
  NimBLERemoteCharacteristic* notifyChr = svc ? svc->getCharacteristic(NOTIFY_UUID) : nullptr;
  writeChr = svc ? svc->getCharacteristic(WRITE_UUID) : nullptr;
  if (!notifyChr || !writeChr || !notifyChr->subscribe(true, onNotify)) {
    Serial.println("[BMS] Service/Characteristics nicht gefunden");
    client->disconnect();
    return false;
  }
  Serial.printf("[BMS] Verbunden, RSSI %d\n", client->getRssi());
  return true;
}

static void bmsTask(void*) {
  NimBLEClient* client = NimBLEDevice::createClient();
  client->setConnectTimeout(10000);
  NimBLERemoteCharacteristic* writeChr = nullptr;
  jbd::FrameAssembler assembler;
  bool rawLogged = false;

  for (;;) {
    if (!client->isConnected()) {
      stateSetConnected(false);
      rawLogged = false;
      if (!connectBms(client, writeChr)) { vTaskDelay(pdMS_TO_TICKS(POLL_MS)); continue; }
      stateSetConnected(true);
    }

    xQueueReset(chunkQueue);
    assembler.reset();
    writeChr->writeValue(jbd::CMD_BASIC, sizeof(jbd::CMD_BASIC), false);

    bool ok = false;
    Chunk c;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(RESPONSE_TIMEOUT_MS);
    while (!ok && xTaskGetTickCount() < deadline) {
      if (xQueueReceive(chunkQueue, &c, pdMS_TO_TICKS(100)) == pdTRUE) ok = assembler.feed(c.data, c.len);
    }

    jbd::Basic b;
    if (!ok) {
      Serial.println("[BMS] Keine gueltige Antwort");
    } else if (!jbd::parseBasic(assembler.frame(), assembler.frameLen(), b)) {
      Serial.print("[BMS] Rahmen nicht parsebar: ");
      printHex(assembler.frame(), assembler.frameLen());
    } else {
      if (!rawLogged) {
        Serial.print("[BMS] Rohframe 03: ");
        printHex(assembler.frame(), assembler.frameLen());
        rawLogged = true;
      }
      stateSetBasic(b);
      printBasic(b);
    }
    vTaskDelay(pdMS_TO_TICKS(POLL_MS));
  }
}

static WebServer server(80);

static void handleStatus() {
  BmsSnapshot s = stateGet();
  char buf[512];
  uint32_t now = millis();
  size_t n = model::statusJson(buf, sizeof(buf), s.valid ? &s.basic : nullptr, s.connected,
                               (now - s.updated_ms) / 1000, now / 1000);
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (n == 0) server.send(500, "application/json", "{\"error\":\"buffer\"}");
  else server.send(200, "application/json", buf);
}

static void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    Serial.printf("[WLAN] Verbunden, IP %s, http://%s.local/\n",
      WiFi.localIP().toString().c_str(), HOSTNAME);
  } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    Serial.printf("[WLAN] Getrennt, Grund %u\n", info.wifi_sta_disconnected.reason);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("WomoESP: BMS-Monitor");
  stateInit();
  chunkQueue = xQueueCreate(8, sizeof(Chunk));

  WiFi.onEvent(onWifiEvent);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  MDNS.begin(HOSTNAME);
  MDNS.addService("http", "tcp", 80);

  server.on("/", [] { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/api/status", handleStatus);
  server.begin();

  NimBLEDevice::init("");
  xTaskCreatePinnedToCore(bmsTask, "bms", BMS_TASK_STACK, nullptr, 1, nullptr, 0);
}

void loop() {
  server.handleClient();
  vTaskDelay(pdMS_TO_TICKS(2));
}
