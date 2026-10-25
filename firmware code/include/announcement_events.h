#pragma once

// Versioned JSON events over USB Serial. No network credentials are included.
void beginAnnouncementEvents();
void serviceAnnouncementEvents();
void beginAnnouncementRequest();
void finishAnnouncementRequest(bool success);
void reportArrivalPlayback(const char *phase, int routeNumber, int minutes,
                           const char *destination);
void reportNoBusesPlayback(const char *phase);

void reportStopSelected(const char *commandId);
