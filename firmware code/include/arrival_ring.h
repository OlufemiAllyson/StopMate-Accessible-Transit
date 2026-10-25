#pragma once
#include "rts_predictions.h"

bool beginArrivalRing();
// Only call these in response to an explicit schedule request.
void clearArrivalRing();
void showArrivalRing(const ArrivalSnapshot &snapshot);
