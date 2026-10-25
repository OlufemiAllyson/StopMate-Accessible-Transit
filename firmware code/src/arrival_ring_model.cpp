#include "arrival_ring_model.h"

int arrivalLedPosition(int minutes) {
  if (minutes < 0) return -1;
  if (minutes == 0) return 0;
  if (minutes >= 60) return 7;
  return minutes / 10 + 1;
}

ArrivalRingState makeArrivalRingState(const ArrivalSnapshot &snapshot) {
  ArrivalRingState state;
  for (size_t i = 0; i < snapshot.count && i < MONITORED_ROUTE_COUNT; ++i) {
    const Arrival &arrival = snapshot.arrivals[i];
    const int position = arrivalLedPosition(arrival.minutes);
    if (position < 0 || arrival.routeIndex >= MONITORED_ROUTE_COUNT) continue;
    size_t &count = state.counts[position];
    bool duplicate = false;
    for (size_t j = 0; j < count; ++j) duplicate |= state.routes[position][j] == arrival.routeIndex;
    if (!duplicate && count < MONITORED_ROUTE_COUNT) state.routes[position][count++] = arrival.routeIndex;
  }
  return state;
}

void renderArrivalRing(const ArrivalRingState &state, uint32_t phase,
                       uint32_t (&colors)[ARRIVAL_LED_COUNT]) {
  for (size_t position = 0; position < ARRIVAL_LED_COUNT; ++position) {
    colors[position] = 0;
    const size_t count = state.counts[position];
    if (!count || count > MONITORED_ROUTE_COUNT) continue;
    const size_t route = state.routes[position][phase % count];
    if (route < MONITORED_ROUTE_COUNT) colors[position] = MONITORED_ROUTES[route].color;
  }
}
