#include "announcement_events.h"
#include "route_config.h"
#include "selected_stop.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_system.h>

namespace {
char bootId[17];
uint32_t sequence = 0;
uint32_t requestId = 0;
uint32_t lastHeartbeat = 0;

void emit(JsonDocument &event, const char *type) {
  event["version"] = 1;
  event["deviceId"] = "stopmate-0440";
  event["stopId"] = selectedStopId();
  event["supportsStopSelection"] = true;
  event["bootId"] = bootId;
  event["sequence"] = ++sequence;
  event["requestId"] = requestId;
  event["type"] = type;
  event["uptimeMs"] = millis();
  // One Serial write prevents other tasks' log lines splitting the JSON event.
  char line[1024] = "STOPMATE_EVENT ";
  constexpr size_t prefixLength = sizeof("STOPMATE_EVENT ") - 1;
  if (measureJson(event) > sizeof(line) - prefixLength - 2) return;
  const size_t length = serializeJson(event, line + prefixLength, sizeof(line) - prefixLength - 1);
  line[prefixLength + length] = '\n';
  Serial.write(reinterpret_cast<const uint8_t *>(line), prefixLength + length + 1);
}
}

void beginAnnouncementEvents() {
  snprintf(bootId, sizeof(bootId), "%08lx%08lx",
           static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()));
  lastHeartbeat = millis();
  JsonDocument event;
  emit(event, "ready");
}

void serviceAnnouncementEvents() {
  if (millis() - lastHeartbeat < 10000) return;
  lastHeartbeat = millis();
  JsonDocument event;
  emit(event, "heartbeat");
}

void beginAnnouncementRequest() {
  ++requestId;
  JsonDocument event;
  emit(event, "request_started");
}

void finishAnnouncementRequest(bool success) {
  JsonDocument event;
  event["success"] = success;
  emit(event, "request_finished");
}

void reportArrivalPlayback(const char *phase, int routeNumber, int minutes,
                           const char *destination) {
  JsonDocument event;
  event["phase"] = phase;
  event["kind"] = "arrival";
  char route[12];
  snprintf(route, sizeof(route), "%d", routeNumber);
  event["route"] = route;
  event["destination"] = destination;
  event["minutesAway"] = minutes;
  uint32_t rgb = 0x64748B;
  for (const auto &configured : MONITORED_ROUTES)
    if (configured.spokenNumber == routeNumber) { rgb = configured.color; break; }
  char color[8];
  snprintf(color, sizeof(color), "#%06lx", static_cast<unsigned long>(rgb));
  event["color"] = color;
  char text[240];
  if (!minutes) snprintf(text, sizeof(text), "Bus %d for route %s is arriving now.", routeNumber, destination);
  else snprintf(text, sizeof(text), "Bus %d for route %s will arrive in %d %s.",
                routeNumber, destination, minutes, minutes == 1 ? "minute" : "minutes");
  event["text"] = text;
  emit(event, "announcement");
}

void reportNoBusesPlayback(const char *phase) {
  JsonDocument event;
  event["phase"] = phase;
  event["kind"] = "no_buses";
  event["text"] = "No buses available.";
  emit(event, "announcement");
}

void reportStopSelected(const char *commandId) {
  JsonDocument event;
  event["commandId"] = commandId;
  emit(event, "stop_selected");
}
