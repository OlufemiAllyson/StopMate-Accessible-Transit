#pragma once
#include "route_config.h"
#include <ArduinoJson.h>

struct Arrival {
  size_t routeIndex;
  int minutes;
};

struct ArrivalSnapshot {
  Arrival arrivals[MONITORED_ROUTE_COUNT] = {};
  size_t count = 0;
  size_t skipped = 0;
};

// Pure selection logic, also exercised by host-side tests.
// Selects the earliest usable prediction per configured route/direction.
ArrivalSnapshot selectArrivals(JsonArrayConst predictions, const char *stopId);

// True for usable arrivals or a confirmed empty/no-service response. False for
// API errors, malformed data, or a nonempty list containing only skipped entries.
bool parseArrivalResponse(JsonObjectConst response, const char *stopId, ArrivalSnapshot &snapshot);
