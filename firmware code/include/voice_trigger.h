#pragma once
#include <cstddef>
#include <cstdint>

// INMP441: BCLK 27, WS 32, SD 33, L/R high (right channel).
bool beginVoiceTrigger();
bool takeVoiceTrigger();
// Called as a pair from the main loop around blocking announcements.
void pauseVoiceTrigger();
void resumeVoiceTrigger();

// Main-task only, between pauseVoiceTrigger() and resumeVoiceTrigger().
// Warms the mic, counts down, then captures 16 kHz PCM16 using recognition gain.
// Rejects dropped samples. Emits the start cue after microphone warmup.
// Buffer/count include 300 ms before the cue plus the requested duration.
constexpr size_t VOICE_RECORDING_PREROLL_SAMPLES = 4800;
bool capturePausedVoiceAudio(int16_t *samples, size_t count);
