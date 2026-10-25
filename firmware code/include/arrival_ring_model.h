#pragma once
#include "rts_predictions.h"
#include <stdint.h>

constexpr size_t ARRIVAL_LED_COUNT = 8;

struct ArrivalRingState {
  size_t routes[ARRIVAL_LED_COUNT][MONITORED_ROUTE_COUNT] = {};
  size_t counts[ARRIVAL_LED_COUNT] = {};
};

// D1: due; D2: 1-9; D3: 10-19; ...; D7: 50-59; D8: 60+.
int arrivalLedPosition(int minutes);
ArrivalRingState makeArrivalRingState(const ArrivalSnapshot &snapshot);
// phase selects each colliding route in turn; no ETA aging or network access.
void renderArrivalRing(const ArrivalRingState &state, uint32_t phase,
                       uint32_t (&colors)[ARRIVAL_LED_COUNT]);
