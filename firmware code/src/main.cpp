#include <Arduino.h>
#include "announcement.h"
#include "rts_client.h"
#include "voice_trigger.h"
#include "serial_recorder.h"
#include "arrival_ring.h"
#include "ultrasonic_trigger.h"
#include "arrival_display.h"
#include "announcement_events.h"
#include "selected_stop.h"

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
  beginSelectedStop();
  beginNetwork();
  beginArrivalRing();
  audioReady = beginAudio();
  // Initialize the 1-bit SD card before repurposing its unused DAT3/GPIO13.
  beginArrivalDisplay();
  if (audioReady) beginVoiceTrigger();
  printRecordingHelp();
  waitForButtonRelease();
  beginUltrasonicTrigger();
  beginAnnouncementEvents();
  Serial.println("Ready: wave, press the button, say Hey StopMate, or type schedule. No startup lookup or announcement.");
}

void loop() {
  serviceAnnouncementEvents();
  serviceNetwork();
  if (pollRecordingCommands()) {
    waitForButtonRelease();
    resetUltrasonicTrigger();
  }
  const bool serialRequest = takeSerialScheduleRequest();
  const bool voiceRequest = takeVoiceTrigger();
  const bool waveRequest = pollUltrasonicTrigger();
  bool announce = audioReady && (voiceRequest || serialRequest || waveRequest);
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
    resetUltrasonicTrigger();
  }
  idleAudio();
}
