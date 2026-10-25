#pragma once

void beginUltrasonicTrigger();
bool pollUltrasonicTrigger();
// Call after blocking recording/announcement work; require a fresh clear area.
void resetUltrasonicTrigger();
