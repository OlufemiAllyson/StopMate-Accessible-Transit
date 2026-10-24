#include <Arduino.h>
#include "announcement.h"
#include "rts_client.h"
#include "voice_trigger.h"
#include "serial_recorder.h"

namespace {
constexpr uint8_t BUTTON_PIN = 12;
constexpr uint32_t DEBOUNCE_MS = 40;
bool audioReady = false;
bool buttonArmed = false;
int lastReading = LOW;
int stableReading = LOW;
uint32_t changedAt = 0;

void waitForButtonRelease() {
  // Require a fresh, debounced release after blocking network/audio work.
  // A held button must never cause repeated requests.
  buttonArmed = false;
  lastReading = stableReading = LOW;
  changedAt = millis();
}
}

void setup() {
  Serial.begin(115200);
  // Joystick SW connects GPIO 12 to GND when pressed.
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  delay(500);
  beginNetwork();
  audioReady = beginAudio();
  if (audioReady) announceArrivals();
  if (audioReady) beginVoiceTrigger();
  printRecordingHelp();
  waitForButtonRelease();
}

void loop() {
  serviceNetwork();
  if (pollRecordingCommands()) waitForButtonRelease();
  bool announce = audioReady && takeVoiceTrigger();
  const int reading = digitalRead(BUTTON_PIN);
  const uint32_t now = millis();
  if (reading != lastReading) {
    lastReading = reading;
    changedAt = now;
  }
  if (reading != stableReading && now - changedAt >= DEBOUNCE_MS) {
    stableReading = reading;
    if (stableReading == HIGH) {
      buttonArmed = true;
    } else if (buttonArmed) {
      buttonArmed = false;
      if (audioReady) {
        Serial.println("Button pressed: fetching fresh arrivals.");
        announce = true;
      }
      waitForButtonRelease();
    }
  }
  if (announce) {
    pauseVoiceTrigger();
    announceArrivals();
    resumeVoiceTrigger();
    waitForButtonRelease();
  }
  idleAudio();
}
