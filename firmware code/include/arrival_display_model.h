#pragma once
#include "rts_predictions.h"
#include <cstdint>

enum class ArrivalDisplayStatus { Idle, Loading, Arrivals, Error };
struct ArrivalDisplayFrame {
  char rows[2][17] = {};  // Two padded, null-terminated 16-character rows.
};

ArrivalDisplayFrame renderArrivalDisplay(ArrivalDisplayStatus status,
                                        const ArrivalSnapshot &snapshot,
                                        uint32_t phase, const char *stopId = RTS_STOP_ID);
