#include "arrival_display.h"
#include "arrival_display_model.h"
#include "selected_stop.h"
#include <Arduino.h>
#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include <freertos/semphr.h>
#include <cstring>

namespace {
constexpr int LCD_SDA = 23;
constexpr int LCD_SCL = 13;
constexpr uint32_t ROUTE_INTERVAL_MS = 3000;
// Standard Freenove/PCF8574 LCD1602 mapping: RS0, RW1, EN2, BL3, D4-D7.
// Auto-detect the address, but avoid probing the backpack's LCD pin mapping.
hd44780_I2Cexp lcd(I2Cexp_BOARD_YWROBOT);
SemaphoreHandle_t stateLock = nullptr;
ArrivalSnapshot savedSnapshot;
char savedStopId[5] = "0440";
ArrivalDisplayStatus savedStatus = ArrivalDisplayStatus::Idle;
uint32_t savedAt = 0;
bool ready = false;

void displayTask(void *) {
  ArrivalDisplayFrame previous;
  while (true) {
    xSemaphoreTake(stateLock, portMAX_DELAY);
    const ArrivalSnapshot snapshot = savedSnapshot;
    const ArrivalDisplayStatus status = savedStatus;
    const uint32_t epoch = savedAt;
    char stopId[5];
    memcpy(stopId, savedStopId, sizeof(stopId));
    xSemaphoreGive(stateLock);
    const auto next = renderArrivalDisplay(status, snapshot, (millis() - epoch) / ROUTE_INTERVAL_MS, stopId);
    if (std::memcmp(&next, &previous, sizeof(next))) {
      for (uint8_t row = 0; row < 2; ++row) {
        if (lcd.setCursor(0, row) != 0 || lcd.print(next.rows[row]) != 16) {
          Serial.println("LCD write failed; display task stopped. Check wiring and restart.");
          vTaskDelete(nullptr);
          return;
        }
      }
      previous = next;
      Serial.printf("LCD: [%s] [%s]\n", next.rows[0], next.rows[1]);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void replaceState(ArrivalDisplayStatus status, const ArrivalSnapshot &snapshot = {}) {
  if (!ready) return;
  xSemaphoreTake(stateLock, portMAX_DELAY);
  savedStatus = status;
  savedSnapshot = snapshot;
  strlcpy(savedStopId, selectedStopId(), sizeof(savedStopId));
  savedAt = millis();
  xSemaphoreGive(stateLock);
}
}

bool beginArrivalDisplay() {
  if (ready) return true;
  if (!Wire.setPins(LCD_SDA, LCD_SCL) || !Wire.begin()) {
    Serial.println("LCD I2C initialization failed; announcements remain available.");
    return false;
  }
  Wire.setClock(100000);
  Wire.setTimeOut(20);
  unsigned found = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf("LCD bus: I2C device at 0x%02X on SDA23/SCL13.\n", address);
      ++found;
    }
  }
  if (!found) {
    Serial.println("LCD not ready: no I2C devices on SDA23/SCL13. Check power and wiring.");
    return false;
  }
  const int status = lcd.begin(16, 2);
  if (status != 0) {
    Serial.printf("LCD not ready (status %d). Check power, SDA23 and SCL13; announcements remain available.\n", status);
    return false;
  }
  lcd.backlight();
  stateLock = xSemaphoreCreateMutex();
  if (!stateLock) return false;
  savedAt = millis();
  strlcpy(savedStopId, selectedStopId(), sizeof(savedStopId));
  if (xTaskCreatePinnedToCore(displayTask, "arrival_lcd", 4096, nullptr, 1, nullptr, 1) != pdPASS) {
    vSemaphoreDelete(stateLock);
    stateLock = nullptr;
    Serial.println("LCD task initialization failed.");
    return false;
  }
  ready = true;
  Serial.println("LCD1602 ready on SDA23/SCL13; routes alternate every 3 seconds after a schedule request.");
  return true;
}

void showArrivalDisplayLoading() { replaceState(ArrivalDisplayStatus::Loading); }
void showArrivalDisplayError() { replaceState(ArrivalDisplayStatus::Error); }
void showArrivalDisplay(const ArrivalSnapshot &snapshot) {
  replaceState(ArrivalDisplayStatus::Arrivals, snapshot);
}
