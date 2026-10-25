#include "arrival_display_model.h"
#include <cassert>
#include <cstring>
#include <iostream>

void expect(const ArrivalDisplayFrame &frame, const char *top, const char *bottom) {
  assert(std::strlen(frame.rows[0]) == 16 && std::strlen(frame.rows[1]) == 16);
  assert(std::strcmp(frame.rows[0], top) == 0);
  assert(std::strcmp(frame.rows[1], bottom) == 0);
}

int main() {
  using Status = ArrivalDisplayStatus;
  ArrivalSnapshot snapshot;
  expect(renderArrivalDisplay(Status::Idle, snapshot, 0), "StopMate 0440   ", "Wave for buses  ");
  expect(renderArrivalDisplay(Status::Idle, snapshot, 0, "0441"), "StopMate 0441   ", "Wave for buses  ");
  expect(renderArrivalDisplay(Status::Loading, snapshot, 0), "Checking buses  ", "Please wait...  ");
  expect(renderArrivalDisplay(Status::Error, snapshot, 0), "No buses        ", "available       ");
  expect(renderArrivalDisplay(Status::Arrivals, snapshot, 0), "No buses        ", "available       ");
  snapshot.count = 2;
  snapshot.arrivals[0] = {0, 6};
  snapshot.arrivals[1] = {1, 25};
  expect(renderArrivalDisplay(Status::Arrivals, snapshot, 0), "Bus 8: 6 min    ", "UF Health Shands");
  expect(renderArrivalDisplay(Status::Arrivals, snapshot, 1), "Bus 13: 25 min  ", "Cottage Grove   ");
  const auto first = renderArrivalDisplay(Status::Arrivals, snapshot, 0);
  const auto repeated = renderArrivalDisplay(Status::Arrivals, snapshot, 100);
  assert(std::memcmp(&first, &repeated, sizeof(first)) == 0); // no countdown
  snapshot.count = 1;
  snapshot.arrivals[0] = {1, 0};
  expect(renderArrivalDisplay(Status::Arrivals, snapshot, 99), "Bus 13: DUE     ", "Cottage Grove   ");
  snapshot.arrivals[0].minutes = 1440;
  expect(renderArrivalDisplay(Status::Arrivals, snapshot, 0), "Bus 13: 1440 min", "Cottage Grove   ");
  snapshot.arrivals[0] = {MONITORED_ROUTE_COUNT, 5};
  expect(renderArrivalDisplay(Status::Arrivals, snapshot, 0), "No buses        ", "available       ");
  snapshot.count = MONITORED_ROUTE_COUNT + 99;
  snapshot.arrivals[0] = {0, -1};
  snapshot.arrivals[1] = {1, 1441};
  expect(renderArrivalDisplay(Status::Arrivals, snapshot, 0), "No buses        ", "available       ");
  std::cout << "LCD route cycling, fixed ETAs, due/long arrivals, padding, and empty/error states passed.\n";
}
