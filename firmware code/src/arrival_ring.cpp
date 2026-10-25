#include "arrival_ring.h"
#include "arrival_ring_model.h"
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <freertos/semphr.h>
#include <cstring>

namespace {
constexpr uint8_t LED_PIN = 21;
constexpr uint8_t LED_BRIGHTNESS = 64;  // Approximately 25% of full brightness.
constexpr uint32_t COLOR_INTERVAL_MS = 1000;
// Rotate the module with D1 at the top. Adjust these for another mounting.
constexpr size_t FIRST_LED = 0;
constexpr bool REVERSE_ORDER = false;
Adafruit_NeoPixel ring(ARRIVAL_LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
SemaphoreHandle_t stateLock = nullptr;
ArrivalRingState savedState;
uint32_t savedAt = 0;
bool ready = false;

void displayTask(void *) {
  uint32_t previous[ARRIVAL_LED_COUNT] = {};
  while (true) {
    ArrivalRingState state;
    uint32_t epoch;
    xSemaphoreTake(stateLock, portMAX_DELAY);
    state = savedState;
    epoch = savedAt;
    xSemaphoreGive(stateLock);
    uint32_t colors[ARRIVAL_LED_COUNT];
    renderArrivalRing(state, (millis() - epoch) / COLOR_INTERVAL_MS, colors);
    if (memcmp(colors, previous, sizeof(colors))) {
      for (size_t i = 0; i < ARRIVAL_LED_COUNT; ++i) {
        const size_t physical = (FIRST_LED + (REVERSE_ORDER ? ARRIVAL_LED_COUNT - i : i)) % ARRIVAL_LED_COUNT;
        ring.setPixelColor(physical, colors[i]);
      }
      ring.show();  // ESP32 RMT handles LED timing independently of audio I2S.
      memcpy(previous, colors, sizeof(colors));
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void replaceState(const ArrivalRingState &state) {
  if (!ready) return;
  xSemaphoreTake(stateLock, portMAX_DELAY);
  savedState = state;
  savedAt = millis();
  xSemaphoreGive(stateLock);
}
}

bool beginArrivalRing() {
  if (ready) return true;
  ring.begin();
  if (!ring.getPixels()) {
    Serial.println("LED ring initialization failed: no pixel buffer.");
    return false;
  }
  ring.setBrightness(LED_BRIGHTNESS);
  ring.clear();
  ring.show();
  stateLock = xSemaphoreCreateMutex();
  if (!stateLock) return false;
  if (xTaskCreatePinnedToCore(displayTask, "arrival_leds", 4096, nullptr, 1, nullptr, 1) != pdPASS) {
    vSemaphoreDelete(stateLock);
    stateLock = nullptr;
    Serial.println("LED ring task initialization failed.");
    return false;
  }
  ready = true;
  Serial.println("LED ring ready on GPIO 21: D1=due, D2=1-9, D3=10-19, D4=20-29, D5=30-39, D6=40-49, D7=50-59, D8=60+ minutes.");
  return true;
}

void clearArrivalRing() {
  replaceState({});
}

void showArrivalRing(const ArrivalSnapshot &snapshot) {
  replaceState(makeArrivalRingState(snapshot));
  for (size_t i = 0; i < snapshot.count && i < MONITORED_ROUTE_COUNT; ++i) {
    const auto &arrival = snapshot.arrivals[i];
    const int position = arrivalLedPosition(arrival.minutes);
    if (position < 0 || arrival.routeIndex >= MONITORED_ROUTE_COUNT) continue;
    const auto &route = MONITORED_ROUTES[arrival.routeIndex];
    Serial.printf("LED D%d: route %s, %d minutes, RTS color #%06lX.\n", position + 1,
                  route.apiRoute, arrival.minutes, static_cast<unsigned long>(route.color));
  }
  Serial.println("LED times frozen until the next request; shared positions alternate route colors.");
}
