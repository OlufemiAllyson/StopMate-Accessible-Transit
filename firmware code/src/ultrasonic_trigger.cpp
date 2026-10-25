#include <Arduino.h>
#include "ultrasonic_trigger.h"
#include "wave_detector.h"

namespace {
constexpr uint8_t TRIG_PIN = 18;
constexpr uint8_t ECHO_PIN = 19;
constexpr uint32_t SAMPLE_INTERVAL_MS = 70;
constexpr uint32_t ECHO_TIMEOUT_US = 15000;
WaveDetector detector;
uint32_t lastSample = 0;
}

void beginUltrasonicTrigger() {
  pinMode(TRIG_PIN, OUTPUT);
  digitalWrite(TRIG_PIN, LOW);
  pinMode(ECHO_PIN, INPUT_PULLDOWN);
  detector.reset(millis(), 0);
  lastSample = millis();
  Serial.println("Wave trigger ready: TRIG GPIO18, ECHO GPIO19 (voltage divider). Wave 2-30 cm away after clearing the area.");
}

void resetUltrasonicTrigger() {
  detector.reset(millis());
  lastSample = millis();
}

bool pollUltrasonicTrigger() {
  const uint32_t now = millis();
  if (now - lastSample < SAMPLE_INTERVAL_MS) return false;
  lastSample = now;
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  // Bounded wait on the main task; microphone capture runs independently.
  const unsigned long duration = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT_US);
  const uint32_t distanceCm = duration == 0 ? 0 : (duration + 29) / 58;
  if (!detector.sample(millis(), distanceCm)) return false;
  Serial.printf("Wave detected at %lu cm: fetching fresh arrivals.\n",
                static_cast<unsigned long>(distanceCm));
  return true;
}
