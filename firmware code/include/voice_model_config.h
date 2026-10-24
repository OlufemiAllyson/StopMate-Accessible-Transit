#pragma once

// Share the trigger policy between firmware and recorded-audio replay tests.
constexpr float VOICE_WAKE_THRESHOLD = 0.90f;
constexpr float VOICE_WAKE_MARGIN = 0.20f;
constexpr unsigned VOICE_REQUIRED_HITS = 2;
