#include <cassert>
#include <cstdint>
#include <cstdio>
#include "wave_detector.h"

int main() {
  WaveDetector detector;
  detector.reset(0, 0);
  // A hand present at boot must not request arrivals.
  for (uint32_t t = 0; t < 1000; t += 70) assert(!detector.sample(t, 20));
  assert(!detector.sample(1000, 0));
  assert(!detector.sample(1300, 100));
  // A single near glitch does not trigger; intermediate readings cancel it.
  assert(!detector.sample(1370, 20));
  assert(!detector.sample(1440, 35));
  assert(!detector.sample(1510, 20));
  assert(detector.sample(1580, 20));
  // A held hand cannot retrigger even after the cooldown expires.
  for (uint32_t t = 1650; t < 6000; t += 70) assert(!detector.sample(t, 20));
  assert(!detector.sample(6000, 40));
  assert(!detector.sample(6200, 35)); // interrupts stable clearance
  assert(!detector.sample(6300, 0));
  assert(!detector.sample(6600, 0));
  assert(!detector.sample(6670, 30));
  assert(!detector.sample(7000, 30)); // stale near reading discarded
  assert(detector.sample(7070, 30));
  // After blocking playback, old samples and clearance are discarded.
  detector.reset(10000);
  assert(!detector.sample(11000, 0));
  assert(!detector.sample(12999, 0));
  assert(!detector.sample(13000, 0));
  assert(!detector.sample(13300, 0));
  assert(!detector.sample(13370, 2));
  assert(detector.sample(13440, 2));
  // Unsigned time arithmetic survives millis() rollover.
  detector.reset(UINT32_MAX - 1000);
  assert(!detector.sample(1998, 0));
  assert(!detector.sample(2000, 0));
  assert(!detector.sample(2300, 0));
  assert(!detector.sample(2370, 15));
  assert(detector.sample(2440, 15));
  // An absent sensor/no echoes never triggers.
  detector.reset(0, 0);
  for (uint32_t t = 0; t < 10000; t += 70) assert(!detector.sample(t, 0));
  std::puts("Wave detector tests passed.");
}
