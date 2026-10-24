// Host test: compile with src/rts_predictions.cpp and ArduinoJson's include path.
#include "rts_predictions.h"
#include <cassert>
#include <fstream>
#include <iostream>

int main(int argc, char **argv) {
  JsonDocument doc;
  // Empty service is a successful lookup; arbitrary errors must not become
  // a spoken claim that no buses are available.
  auto responseIsUsable = [&](const char *json, size_t expectedCount = 0) {
    JsonDocument response;
    assert(!deserializeJson(response, json));
    ArrivalSnapshot selected;
    const bool ok = parseArrivalResponse(response.as<JsonObjectConst>(), "0440", selected);
    if (ok) assert(selected.count == expectedCount);
    return ok;
  };
  assert(responseIsUsable(R"({"error":[{"stpid":"0440","msg":"No service scheduled"}]})"));
  assert(responseIsUsable(R"({"prd":[]})"));
  assert(responseIsUsable(R"({"error":[],"prd":[]})"));
  assert(!responseIsUsable(R"({"error":[{"stpid":"0440","msg":"Invalid key"}]})"));
  assert(!responseIsUsable(R"({"error":[{"stpid":"0440","msg":"No service scheduled"},{"msg":"Rate limit exceeded"}]})"));
  assert(!responseIsUsable(R"({"error":[{"stpid":"9999","msg":"No service scheduled"}]})"));
  assert(!responseIsUsable(R"({"error":"No service scheduled"})"));
  assert(!responseIsUsable(R"({"prd":{}})"));
  assert(!responseIsUsable(R"({})"));
  assert(!responseIsUsable(R"({"prd":[{"stpid":"0440","typ":"A","rt":"8","rtdir":"INBOUND","prdctdn":"DLY"}]})"));
  assert(responseIsUsable(R"({"prd":[{"stpid":"0440","typ":"A","rt":"8","rtdir":"INBOUND","prdctdn":"3"}]})", 1));
  assert(!deserializeJson(doc, R"JSON([
    {"stpid":"0440","typ":"A","rt":"13","rtdir":"INBOUND","prdctdn":"38"},
    {"stpid":"0440","typ":"A","rt":"8","rtdir":"INBOUND","prdctdn":"9"},
    {"stpid":"0440","typ":"A","rt":"13","rtdir":"INBOUND","prdctdn":"13"},
    {"stpid":"0440","typ":"A","rt":"8","rtdir":"INBOUND","prdctdn":5},
    {"stpid":"0440","typ":"A","rt":"13","rtdir":"OUTBOUND","prdctdn":"1"},
    {"stpid":"9999","typ":"A","rt":"8","rtdir":"INBOUND","prdctdn":"1"},
    {"stpid":"0440","typ":"D","rt":"8","rtdir":"INBOUND","prdctdn":"1"},
    {"stpid":"0440","typ":"A","rt":"99","rtdir":"INBOUND","prdctdn":"1"},
    {"stpid":"0440","typ":"A","rt":"8","rtdir":"INBOUND","prdctdn":"2","dly":true}
  ])JSON"));
  auto snapshot = selectArrivals(doc.as<JsonArrayConst>(), "0440");
  assert(snapshot.count == 2 && snapshot.skipped == 5);
  assert(MONITORED_ROUTES[snapshot.arrivals[0].routeIndex].audioRoute == Route::UFHealthShands);
  assert(snapshot.arrivals[0].minutes == 5);
  assert(MONITORED_ROUTES[snapshot.arrivals[1].routeIndex].audioRoute == Route::CottageGrove);
  assert(snapshot.arrivals[1].minutes == 13);

  // Treat DUE as now, deduplicate it against later trips of the same route,
  // and reject misleading countdown strings rather than coercing them to zero.
  assert(!deserializeJson(doc, R"JSON([
    {"stpid":"0440","typ":"A","rt":"13","rtdir":"INBOUND","prdctdn":"10"},
    {"stpid":"0440","typ":"A","rt":"13","rtdir":"INBOUND","prdctdn":"DUE"},
    {"stpid":"0440","typ":"A","rt":"8","rtdir":"INBOUND","prdctdn":"60"},
    {"stpid":"0440","typ":"A","rt":"8","rtdir":"INBOUND","prdctdn":"DLY"},
    {"stpid":"0440","typ":"A","rt":"8","rtdir":"INBOUND","prdctdn":"5min"},
    {"stpid":"0440","typ":"A","rt":"8","rtdir":"INBOUND","prdctdn":"-1"},
    {"stpid":"0440","typ":"A","rt":"8","rtdir":"INBOUND","prdctdn":"61"},
    {"stpid":"0440","typ":"A","rt":"8","rtdir":"INBOUND","prdctdn":"99999999999999999999"},
    {"stpid":"0440","typ":"A","rt":"8","rtdir":"INBOUND"}
  ])JSON"));
  snapshot = selectArrivals(doc.as<JsonArrayConst>(), "0440");
  assert(snapshot.count == 2 && snapshot.skipped == 6);
  assert(snapshot.arrivals[0].minutes == 0 && snapshot.arrivals[1].minutes == 60);
  assert(MONITORED_ROUTES[snapshot.arrivals[0].routeIndex].audioRoute == Route::CottageGrove);
  assert(!deserializeJson(doc, "[]"));
  assert(selectArrivals(doc.as<JsonArrayConst>(), "0440").count == 0);

  if (argc > 1) {
    std::ifstream live(argv[1]);
    assert(live && !deserializeJson(doc, live));
    snapshot = selectArrivals(doc["bustime-response"]["prd"].as<JsonArrayConst>(), "0440");
    std::cout << "Live RTS fixture: " << snapshot.count << " supported route(s).\n";
  }
  std::cout << "Arrival filtering, ordering, deduplication, DUE, and invalid-input tests passed.\n";
}
