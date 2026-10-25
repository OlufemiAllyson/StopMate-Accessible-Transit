#pragma once
#include "rts_predictions.h"

bool beginArrivalDisplay();
void showArrivalDisplayLoading();
void showArrivalDisplayError();
void showArrivalDisplay(const ArrivalSnapshot &snapshot);
