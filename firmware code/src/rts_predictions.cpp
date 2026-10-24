#include "rts_predictions.h"
#include <cstring>

bool parseArrivalResponse(JsonObjectConst response, const char *stopId, ArrivalSnapshot &snapshot) {
  snapshot = {};
  if (response.isNull()) return false;
  bool noService = false;
  const auto error = response["error"];
  if (!error.isNull()) {
    if (!error.is<JsonArrayConst>()) return false;
    for (JsonObjectConst entry : error.as<JsonArrayConst>()) {
      // BusTime reports this normal no-service condition in its error array.
      // Never treat authentication, rate-limit, or arbitrary API errors as empty.
      if (strcmp(entry["msg"] | "", "No service scheduled") != 0 ||
          strcmp(entry["stpid"] | "", stopId) != 0) return false;
      noService = true;
    }
  }
  const auto predictions = response["prd"];
  if (predictions.is<JsonArrayConst>()) {
    const auto list = predictions.as<JsonArrayConst>();
    if (noService && list.size()) return false;
    snapshot = selectArrivals(list, stopId);
    return list.size() == 0 || snapshot.count > 0;
  }
  return predictions.isNull() && noService;
}

namespace {
bool parseMinutes(JsonVariantConst value, int &minutes) {
  if (value.is<int>()) {
    minutes = value.as<int>();
    return minutes >= 0 && minutes <= 60;
  }
  const char *text = value.as<const char *>();
  if (!text || !*text) return false;
  if (strcmp(text, "DUE") == 0) { minutes = 0; return true; }
  int result = 0;
  for (; *text; ++text) {
    if (*text < '0' || *text > '9') return false;
    result = result * 10 + (*text - '0');
    if (result > 60) return false;
  }
  minutes = result;
  return true;
}
}

ArrivalSnapshot selectArrivals(JsonArrayConst predictions, const char *stopId) {
  ArrivalSnapshot result;
  for (JsonObjectConst prediction : predictions) {
    int minutes = 0;
    if (strcmp(prediction["stpid"] | "", stopId) != 0 ||
        strcmp(prediction["typ"] | "", "A") != 0 ||
        (prediction["dly"] | false) ||
        !parseMinutes(prediction["prdctdn"], minutes)) {
      ++result.skipped;
      continue;
    }
    size_t index = 0;
    for (; index < MONITORED_ROUTE_COUNT; ++index) {
      const auto &route = MONITORED_ROUTES[index];
      if (strcmp(prediction["rt"] | "", route.apiRoute) == 0 &&
          strcmp(prediction["rtdir"] | "", route.direction) == 0) break;
    }
    if (index == MONITORED_ROUTE_COUNT) {
      ++result.skipped;
      continue;
    }
    size_t existing = 0;
    for (; existing < result.count; ++existing) {
      if (result.arrivals[existing].routeIndex == index) break;
    }
    if (existing == result.count) {
      result.arrivals[result.count++] = {index, minutes};
    } else if (minutes < result.arrivals[existing].minutes) {
      result.arrivals[existing].minutes = minutes;
    }
  }
  // A small fixed-size array avoids dynamic allocations in the audio queue.
  for (size_t i = 1; i < result.count; ++i) {
    Arrival current = result.arrivals[i];
    size_t j = i;
    while (j > 0 && result.arrivals[j - 1].minutes > current.minutes) {
      result.arrivals[j] = result.arrivals[j - 1];
      --j;
    }
    result.arrivals[j] = current;
  }
  return result;
}
