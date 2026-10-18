#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("WomoESP: Hallo");
}

void loop() {
  Serial.printf("uptime %lu s\n", millis() / 1000);
  delay(2000);
}
