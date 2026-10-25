#include "arrival_display_model.h"
#include <cstdio>

namespace {
ArrivalDisplayFrame frame(const char *top, const char *bottom) {
  ArrivalDisplayFrame result;
  std::snprintf(result.rows[0], sizeof(result.rows[0]), "%-16.16s", top);
  std::snprintf(result.rows[1], sizeof(result.rows[1]), "%-16.16s", bottom);
  return result;
}
}

ArrivalDisplayFrame renderArrivalDisplay(ArrivalDisplayStatus status,
                                        const ArrivalSnapshot &snapshot,
                                        uint32_t phase, const char *stopId) {
  if (status == ArrivalDisplayStatus::Idle) {
    char title[17];
    std::snprintf(title, sizeof(title), "StopMate %.4s", stopId);
    return frame(title, "Wave for buses");
  }
  if (status == ArrivalDisplayStatus::Loading) return frame("Checking buses", "Please wait...");
  if (status == ArrivalDisplayStatus::Error || !snapshot.count)
    return frame("No buses", "available");

  size_t valid[MONITORED_ROUTE_COUNT];
  size_t count = 0;
  for (size_t i = 0; i < snapshot.count && i < MONITORED_ROUTE_COUNT; ++i) {
    const auto &arrival = snapshot.arrivals[i];
    if (arrival.routeIndex < MONITORED_ROUTE_COUNT && arrival.minutes >= 0 &&
        arrival.minutes <= MAX_PREDICTION_MINUTES) valid[count++] = i;
  }
  if (!count) return frame("No buses", "available");
  const auto &arrival = snapshot.arrivals[valid[phase % count]];
  const auto &route = MONITORED_ROUTES[arrival.routeIndex];
  char top[48];
  if (arrival.minutes == 0) std::snprintf(top, sizeof(top), "Bus %s: DUE", route.apiRoute);
  else std::snprintf(top, sizeof(top), "Bus %s: %d min", route.apiRoute, arrival.minutes);
  return frame(top, route.displayName ? route.displayName : route.apiRoute);
}
