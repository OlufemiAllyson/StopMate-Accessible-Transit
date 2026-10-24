#pragma once

// Start Wi-Fi once; service from the main loop for nonblocking retry/status.
void beginNetwork();
void serviceNetwork();

// Reuse Wi-Fi, fetch a single snapshot, and announce it. Keep Wi-Fi enabled.
// Blocking: each explicit call makes one fresh lookup, without API retries.
void announceArrivals();
