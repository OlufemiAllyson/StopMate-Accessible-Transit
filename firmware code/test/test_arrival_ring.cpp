#include "arrival_ring_model.h"
#include <cassert>
#include <climits>
#include <iostream>

int main() {
  const int minutes[] = {-1, 0, 1, 9, 10, 19, 20, 29, 30, 39, 40, 49, 50, 59, 60, 61, INT_MAX};
  const int positions[] = {-1, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 7};
  for (size_t i = 0; i < sizeof(minutes) / sizeof(minutes[0]); ++i)
    assert(arrivalLedPosition(minutes[i]) == positions[i]);

  ArrivalSnapshot snapshot;
  snapshot.count = 2;
  snapshot.arrivals[0] = {0, 12};
  snapshot.arrivals[1] = {1, 18};
  const auto shared = makeArrivalRingState(snapshot);
  assert(shared.counts[2] == 2);
  uint32_t colors[ARRIVAL_LED_COUNT];
  renderArrivalRing(shared, 0, colors);
  assert(colors[2] == 0xFF7B00);
  for (size_t i = 0; i < ARRIVAL_LED_COUNT; ++i) if (i != 2) assert(colors[i] == 0);
  renderArrivalRing(shared, 1, colors);
  assert(colors[2] == 0x49535A);
  renderArrivalRing(shared, 2, colors);
  assert(colors[2] == 0xFF7B00);
  // Changing the source does not age or mutate the saved display.
  snapshot.arrivals[0].minutes = 0;
  snapshot.arrivals[1].minutes = 75;
  renderArrivalRing(shared, 100, colors);
  assert(colors[2] == 0xFF7B00 && colors[0] == 0 && colors[7] == 0);
  const auto separate = makeArrivalRingState(snapshot);
  renderArrivalRing(separate, 5, colors);
  assert(colors[0] == 0xFF7B00 && colors[7] == 0x49535A && colors[2] == 0);
  // Bad entries cannot access colors outside the configured route table.
  snapshot.arrivals[0] = {MONITORED_ROUTE_COUNT, 10};
  snapshot.arrivals[1] = {1, -1};
  renderArrivalRing(makeArrivalRingState(snapshot), 1, colors);
  for (uint32_t color : colors) assert(color == 0);
  renderArrivalRing(makeArrivalRingState({}), 0, colors);
  for (uint32_t color : colors) assert(color == 0);
  std::cout << "LED boundaries, official colors, collision cycling, frozen snapshots, and empty/invalid inputs passed.\n";
}
