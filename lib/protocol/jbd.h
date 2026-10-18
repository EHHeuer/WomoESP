// JBD-BMS: Frame-Assembler und Parser fuer die Basisdaten (Kommando 0x03).
// Hardwareunabhaengig, wird nativ mit Unity getestet.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace jbd {

static const uint8_t CMD_BASIC[] = {0xDD, 0xA5, 0x03, 0x00, 0xFF, 0xFD, 0x77};
static const uint8_t CMD_CELLS[] = {0xDD, 0xA5, 0x04, 0x00, 0xFF, 0xFC, 0x77};

static const size_t MAX_FRAME = 128;
static const int MAX_NTC = 8;

// Setzt einen Antwortrahmen aus mehreren BLE-Notifications zusammen.
class FrameAssembler {
 public:
  // Liefert true, sobald ein vollstaendiger Rahmen mit gueltiger Pruefsumme vorliegt.
  bool feed(const uint8_t* data, size_t len);
  const uint8_t* frame() const { return buf_; }
  size_t frameLen() const { return done_ ? pos_ : 0; }
  void reset() { pos_ = 0; done_ = false; }

 private:
  bool push(uint8_t b);
  uint8_t buf_[MAX_FRAME];
  size_t pos_ = 0;
  bool done_ = false;
};

// Prueft Start, Ende, Laenge und Pruefsumme eines Rahmens (Antwort oder Kommando).
bool verifyFrame(const uint8_t* f, size_t len);

struct Basic {
  uint16_t voltage_10mV;
  int16_t current_10mA;      // negativ = Entladen
  uint16_t remain_10mAh;
  uint16_t nominal_10mAh;
  uint16_t cycles;
  uint16_t protection;
  uint8_t soc;               // %
  uint8_t fet;
  uint8_t cells;
  uint8_t ntc;
  int16_t temp_01C[MAX_NTC];  // 0,1 °C
};

// Parst einen vollstaendigen, geprueften 0x03-Rahmen.
bool parseBasic(const uint8_t* f, size_t len, Basic& out);

// Unterhalb dieses Stroms (10 mA) gibt es keine Laufzeitschaetzung.
static const int16_t IDLE_CURRENT_10mA = 10;

// Restlaufzeit in Minuten bis leer (Entladen) bzw. bis voll (Laden).
// -1, wenn der Strom zu klein fuer eine sinnvolle Schaetzung ist.
int32_t minutesToEmpty(const Basic& b);
int32_t minutesToFull(const Basic& b);

}  // namespace jbd
