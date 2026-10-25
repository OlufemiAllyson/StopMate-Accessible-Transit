#pragma once

void beginSelectedStop();
const char *selectedStopId();
// Validated against supported RTS stops; persists across restarts.
bool selectStop(const char *stopId);
