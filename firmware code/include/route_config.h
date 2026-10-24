#pragma once
#include "announcement.h"
#include <stddef.h>

constexpr char RTS_STOP_ID[] = "0440";
constexpr char RTS_PREDICTIONS_URL[] = "https://riderts.app/bustime/api/v3/getpredictions";

struct MonitoredRoute {
  const char *apiRoute;
  const char *direction;
  int spokenNumber;
  Route audioRoute;
};

// Add one entry for each route/direction you want announced at RTS_STOP_ID.
// Destination audio must correspond to the direction of travel at this stop.
constexpr MonitoredRoute MONITORED_ROUTES[] = {
    {"8", "INBOUND", 8, Route::UFHealthShands},
    {"13", "INBOUND", 13, Route::CottageGrove},
};
constexpr size_t MONITORED_ROUTE_COUNT = sizeof(MONITORED_ROUTES) / sizeof(MONITORED_ROUTES[0]);
