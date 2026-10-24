#pragma once

enum class Route {
  ButlerPlazaToHub,
  ReitzUnionToLivApartments,
  UFHealthShands,
  CottageGrove,
};

// Blocking playback. Call after audio initialization, from one task at a time.
bool playAnnouncement(int routeNumber, int minutes, Route route);
bool playNoBusesAvailable();
bool beginAudio();
void idleAudio();
