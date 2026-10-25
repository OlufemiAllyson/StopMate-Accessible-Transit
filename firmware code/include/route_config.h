#pragma once
#include "announcement.h"
#include <stddef.h>
#include <stdint.h>

constexpr char RTS_STOP_ID[] = "0440";
constexpr char RTS_PREDICTIONS_URL[] = "https://riderts.app/bustime/api/v3/getpredictions";

struct MonitoredRoute {
  const char *apiRoute;
  const char *direction;
  int spokenNumber;
  Route audioRoute;
  uint32_t color;  // RGB from RTS getroutes.rtclr (verified 2026-10-24).
  const char *displayName;
};

// Add one entry for each route/direction you have audio support for. RTS_STOP_ID is the initial default.
// Destination audio must correspond to the direction of travel at this stop.
constexpr MonitoredRoute MONITORED_ROUTES[] = {
    {"8", "INBOUND", 8, Route::UFHealthShands, 0xFF7B00, "UF Health Shands"},
    {"13", "INBOUND", 13, Route::CottageGrove, 0x49535A, "Cottage Grove"},
};
constexpr size_t MONITORED_ROUTE_COUNT = sizeof(MONITORED_ROUTES) / sizeof(MONITORED_ROUTES[0]);
