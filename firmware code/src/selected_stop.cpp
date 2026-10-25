#include "selected_stop.h"
#include "route_config.h"
#include "supported_stops.h"
#include <Preferences.h>
#include <cstring>

namespace {
char currentStop[5] = "0440";
Preferences preferences;
bool storageReady = false;

bool supported(const char *id) {
  if (!id) return false;
  for (const char *candidate : SUPPORTED_STOP_IDS)
    if (!strcmp(candidate, id)) return true;
  return false;
}
}

void beginSelectedStop() {
  storageReady = preferences.begin("stopmate", false);
  if (!storageReady) return;
  const String saved = preferences.getString("stop", RTS_STOP_ID);
  if (supported(saved.c_str())) strlcpy(currentStop, saved.c_str(), sizeof(currentStop));
}

const char *selectedStopId() { return currentStop; }

bool selectStop(const char *stopId) {
  if (!supported(stopId)) return false;
  if (!strcmp(currentStop, stopId)) return true;
  if (!storageReady || preferences.putString("stop", stopId) != strlen(stopId)) return false;
  strlcpy(currentStop, stopId, sizeof(currentStop));
  return true;
}
