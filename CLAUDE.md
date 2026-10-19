# ESP32 Batteriemonitor

**Stand:** 19.10.2026 | Phase 2 (BMS auslesen) und Phase 5 (WLAN, API) parallel, Display fehlt noch

ESP32 liest ein JBD-BMS per Bluetooth (BLE) und optional einen Victron-MPPT aus, zeigt die Werte auf einem E-Ink-Display und stellt sie per REST-API im Heim-WLAN bereit. Diese Datei ist gleichzeitig Projektkontext für Claude Code und Arbeitsplan zum Abhaken.

---

## Zielbild

| Baustein | Lösung |
|---|---|
| BMS | JBD per BLE (NimBLE-Arduino 2.x) |
| Solarladeregler | Victron per BLE Instant Readout oder VE.Direct (UART), siehe Offene Punkte |
| Anzeige | E-Ink über GxEPD2 |
| Netzwerk | WLAN-Client, REST-API `GET /api/status` (JSON) |
| Toolchain | VS Code, PlatformIO, Framework Arduino |

Zugriff von außerhalb des Heimnetzes nur per VPN (WireGuard oder Tailscale), keine Portweiterleitung auf den ESP.

## Hardware

| Teil | Typ |
|---|---|
| Board | Klassischer ESP32 (ROM 29.07.2019, Rev. 3), USB-UART CP210x an COM7, `board = esp32dev`. Auto-Reset in den Download-Modus klappt nicht ("Wrong boot mode 0x13"). Bewährt: `PLATFORMIO_UPLOAD_FLAGS=--connect-attempts=0` setzen, Upload starten, dann BOOT halten, bis er verbindet. Die Variable auch beim Bauen setzen, sonst baut PlatformIO wegen geänderter Konfiguration neu. Dauerhafte Abhilfe: 10 µF zwischen EN und GND |
| BMS | JBD, BLE-Name `DP04S007L4S150A`, MAC `a5:c2:37:6e:f6:75` (public), Service `0xFF00`. 4 Zellen, 1 NTC, Nennkapazität 140,90 Ah, SW-Version 0xD6. RSSI am ESP -76 bis -94 dBm, Verbindung klappt, aber Reichweite knapp |
| MPPT | Victron, TO-DO: SmartSolar (mit BT) oder BlueSolar (ohne BT) |
| Display | TO-DO: Größe und Controller eintragen |

## Architektur

Getrennte FreeRTOS-Tasks, die über ein zentrales Datenmodell hinter einem Mutex kommunizieren:

| Task | Aufgabe | Takt |
|---|---|---|
| `bms_task` | BLE-Verbindung zum JBD, Abfrage, Parsing, Reconnect | 5-10 s |
| `victron_task` | Passiver BLE-Scan oder UART lesen | laufend |
| `display_task` | E-Ink aktualisieren, periodischer Full Refresh | bei Änderung, max. alle 1-5 min |
| `web` | HTTP-Server mit `/api/status` | auf Anfrage |

Jeder Messwert bekommt einen Zeitstempel der letzten gültigen Aktualisierung. API und Display zeigen veraltete Werte als veraltet an.

Verzeichnisstruktur (Vorschlag):

```
src/            main.cpp, Tasks, HAL (BLE, UART, Display, WLAN)
lib/protocol/   Parser für JBD, Victron, VE.Direct (hardwareunabhängig)
test/           Unity-Tests für die Parser, laufen nativ auf dem PC
tools/          serial_log.py
include/        secrets.h (nicht im Git)
```

---

## Setup in VS Code

1. Extensions installieren: "PlatformIO IDE" und "Claude Code" (Anthropic).
2. Port prüfen: Windows im Geräte-Manager (COM-Port), sonst USB-Treiber CP210x oder CH340 installieren. Linux: User in Gruppe `dialout`.
3. Projekt anlegen: PlatformIO Home, New Project, Board wählen, Framework Arduino.
4. `pio` im Terminal verfügbar machen: Terminal über PlatformIO "New Terminal" öffnen oder `~/.platformio/penv/bin` (Windows: `%USERPROFILE%\.platformio\penv\Scripts`) in den PATH aufnehmen.
5. Diese Datei als `CLAUDE.md` ins Projektverzeichnis legen, Git initialisieren, `.gitignore` mit `.pio/` und `include/secrets.h` anlegen.

### platformio.ini

```ini
[env:esp32]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
monitor_filters = esp32_exception_decoder
lib_deps =
    h2zero/NimBLE-Arduino@^2.0.0

[env:native]
platform = native
test_framework = unity
```

`board` an das tatsächliche Board anpassen (für S3 z. B. `esp32-s3-devkitc-1`).

### Befehle

| Zweck | Befehl |
|---|---|
| Bauen | `pio run -e esp32` |
| Flashen | `pio run -e esp32 -t upload` |
| Monitor (interaktiv) | `pio device monitor` |
| Log für Agent lesen | `python tools/serial_log.py <PORT> 20` |
| Parser-Tests | `pio test -e native` |

Hängt der Upload bei "Connecting...", BOOT-Taste halten, bis er startet.

### tools/serial_log.py

Liest die serielle Ausgabe für eine feste Zeit, damit Claude Code das Log ohne interaktiven Monitor auswerten kann. pyserial ist in der PlatformIO-Umgebung enthalten.

```python
import sys, time
import serial

port = sys.argv[1]
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 15

with serial.Serial(port, 115200, timeout=1) as s:
    s.dtr = False          # IO0 high: normaler Boot
    s.rts = True           # EN low: Reset
    time.sleep(0.1)
    s.rts = False
    end = time.time() + secs
    while time.time() < end:
        line = s.readline()
        if line:
            print(line.decode(errors="replace").rstrip(), flush=True)
```

---

## Protokolle

### JBD BMS (BLE)

| Element | Wert |
|---|---|
| Service | `0xFF00` |
| Notify (Antworten) | `0xFF01` |
| Write (Kommandos) | `0xFF02` |
| Basisdaten abfragen | `DD A5 03 00 FF FD 77` |
| Zellspannungen abfragen | `DD A5 04 00 FF FC 77` |

Antwortrahmen: `DD <cmd> <status> <len> <data...> <chk_hi> <chk_lo> 77`. Prüfsumme: `0x10000 - Summe(status, len, data)`. Antworten kommen über mehrere Notifications verteilt, ein Frame-Assembler setzt sie anhand von `len` zusammen.

Layout Basisdaten (Offset im Datenteil, Big Endian). Gegen eigenen Mitschnitt verifizieren, die Belegung kann je nach Firmware abweichen:

| Offset | Inhalt | Einheit |
|---|---|---|
| 0-1 | Gesamtspannung | 10 mV |
| 2-3 | Strom (vorzeichenbehaftet) | 10 mA |
| 4-5 | Restkapazität | 10 mAh |
| 6-7 | Nennkapazität | 10 mAh |
| 8-9 | Zyklen | Anzahl |
| 16-17 | Schutzstatus | Bitfeld |
| 18 | Softwareversion | |
| 19 | SoC (RSOC) | % |
| 20 | FET-Status | Bitfeld |
| 21 | Zellenzahl | Anzahl |
| 22 | Anzahl NTC | Anzahl |
| 23 ff. | Temperaturen, je 2 Byte | 0,1 K (minus 2731 ergibt 0,1 °C) |

### Victron, Variante A: BLE Instant Readout

Nur SmartSolar mit eingebautem Bluetooth. Werte kommen verschlüsselt in den Advertisements (Hersteller-ID `0x02E1`, AES-128-CTR), kein Verbindungsaufbau nötig. Den Schlüssel je Gerät zeigt die VictronConnect-App. Schlüssel nur in `include/secrets.h`.

### Victron, Variante B: VE.Direct

UART, 19200 Baud, 8N1, Textprotokoll mit Zeilen `<Feld>\t<Wert>`. Relevante Felder: `V` (mV), `I` (mA), `VPV` (mV), `PPV` (W), `CS` (Ladezustand), `H20` (Ertrag heute, 0,01 kWh). Je Block eine Prüfsumme. Pegel je Gerät bis 5 V: Spannungsteiler oder Level-Shifter vor den ESP-RX.

---

## Arbeitsplan

### Phase 0: Setup
- [x] Extensions installiert, Board wird erkannt
- [x] Projekt angelegt, `platformio.ini` übernommen
- [x] Git, `.gitignore`, `tools/serial_log.py` (Remote: github.com/EHHeuer/WomoESP)
- [x] Leeres Programm mit `Serial.println` geflasht, Ausgabe sichtbar

### Phase 1: BMS finden
- [x] BLE-Scan flashen (Code unten), JBD anhand von Name, MAC oder Service `0xFF00` identifizieren
- [x] MAC und Name oben unter Hardware eintragen
- [ ] Mit nRF Connect (Smartphone) Rohframes für `03` und `04` mitschneiden und unter `test/fixtures/` ablegen (`03` vom ESP mitgeschnitten, `04` fehlt noch)

### Phase 2: BMS auslesen
- [x] Parser für JBD-Frames in `lib/protocol/`, Tests mit den Mitschnitten (Basisdaten `03`)
- [x] BLE-Verbindung, Notify abonnieren, Kommandos zyklisch senden
- [x] Werte auf Serial ausgeben, gegen Xiaoxiang-App vergleichen (stimmt überein, 19.10.2026)
- [ ] Reconnect testen: BMS aus und ein, außer Reichweite gehen

### Phase 3: Victron
- [ ] Variante festlegen (A oder B)
- [ ] Parser beziehungsweise Entschlüsselung mit Tests
- [ ] Anbindung, parallel zum BMS stabil

### Phase 4: Display
- [ ] Display mit GxEPD2 in Betrieb nehmen
- [ ] Layout: SoC groß, Spannung, Strom, PV-Leistung, Alter der Daten
- [ ] Partial Refresh plus periodischer Full Refresh

### Phase 5: WLAN und API
- [ ] WLAN-Client mit Zugangsdaten aus `secrets.h`, Reconnect (verbindet, Auto-Reconnect aktiv, Reconnect noch nicht getestet)
- [x] `GET /api/status` mit JSON inkl. Zeitstempeln (`age_s`, `stale` ab 30 s)
- [x] `GET /` simuliert das E-Ink-Display (2,9" oder 4,2", 1 Bit, Teil- und Voll-Refresh), Vorlage für das GxEPD2-Layout
- [ ] Optional: mDNS (`batterie.local`), MQTT für Home Assistant (mDNS läuft)

### Phase 6: Dauerbetrieb
- [ ] 24-h-Test, Heap und Stack beobachten (`ESP.getFreeHeap()`, High-Water-Marks)
- [ ] Watchdog
- [ ] Gehäuse und Stromversorgung

### Startcode Phase 1: BLE-Scan (`src/main.cpp`)

```cpp
#include <NimBLEDevice.h>

class ScanCb : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* d) override {
    bool jbd = d->isAdvertisingService(NimBLEUUID((uint16_t)0xFF00));
    Serial.printf("%s  RSSI %4d  %-20s %s\n",
      d->getAddress().toString().c_str(), d->getRSSI(),
      d->getName().c_str(), jbd ? "<-- JBD?" : "");
  }
};

void setup() {
  Serial.begin(115200);
  delay(1000);
  NimBLEDevice::init("");
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(new ScanCb());
  scan->setActiveScan(true);
  scan->start(10000);  // 10 s, NimBLE 2.x rechnet in ms
}

void loop() {}
```

---

## Regeln für Claude Code

- Nach jeder Änderung selbst bauen. Wo Hardware nötig ist: flashen und das Log mit `tools/serial_log.py` prüfen.
- Parser und Logik hardwareunabhängig halten und mit `pio test -e native` testen, bevor sie auf das Board kommen.
- NimBLE-Arduino 2.x API verwenden (Callbacks mit `const`-Zeigern, Zeiten in ms). Keine Beispiele aus 1.x übernehmen.
- Keine Heap-Allokation in ISRs und BLE-Callbacks. Callbacks kopieren Daten nur in eine Queue.
- FreeRTOS-Tasks mit expliziter Stackgröße anlegen.
- Zugangsdaten und Victron-Schlüssel ausschließlich in `include/secrets.h`, nie in andere Dateien oder Logs.
- **Niemals** `espefuse`, Secure Boot oder Flash Encryption ausführen. Das ist irreversibel.
- Ein Arbeitsschritt pro Durchlauf, danach Checkbox in dieser Datei abhaken und committen.
- Bei Abweichung von dieser Datei (Protokoll, Offsets, Bibliotheksversion) die Datei aktualisieren statt still abzuweichen.

---

## Offene Punkte

1. Boardvariante: vorhanden ist ein klassischer ESP32 ohne PSRAM. RAM-Reserve für BLE plus WLAN in Phase 6 beobachten.
2. Victron-Modell: entscheidet zwischen BLE Instant Readout und VE.Direct.
3. Display: Größe und Controller, davon hängt die GxEPD2-Klasse ab.
4. Stromversorgung des ESP aus der Batterieanlage (Spannungswandler, Eigenverbrauch).
5. JBD-Layout für `03` am 19.10.2026 gegen eigenen Mitschnitt bestätigt (`test/fixtures/jbd_03_basic.hex`). Die Antwort hat 34 statt 25 Byte, ab Offset 25 folgen Zusatzfelder (u. a. Nennkapazität und Restkapazität erneut), die der Parser ignoriert.
6. Restlaufzeit nutzt den Momentanstrom und springt bei Lastwechseln. Glättung über einige Minuten vorsehen.
8. Flash mit BLE plus WLAN zu 83 % belegt (Standard-Partition, 1,25 MB App). Für Display und Victron vermutlich Partition `min_spiffs.csv` (1,9 MB App, OTA bleibt) nötig.
7. Native Tests brauchen GCC (WinLibs per winget installiert). In Terminals, die vor der Installation geöffnet wurden, fehlt GCC im PATH.
