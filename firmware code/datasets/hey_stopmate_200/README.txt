HEY STOPMATE — 200-CLIP SYNTHETIC PROTOTYPE DATASET

All clips: 2 seconds, 16,000 Hz, mono, signed 16-bit PCM WAV.
Total duration: 6 minutes 40 seconds.

                   training    testing    total
hey_stopmate             80         20      100
unknown                  48         12       60
noise                    32          8       40
TOTAL                   160         40      200

EDGE IMPULSE IMPORT
1. Extract training.zip and testing.zip into separate folders.
2. In Data acquisition / Upload data, upload the training WAVs to Training.
3. Upload the testing WAVs separately to Testing. Do not auto-split these files.
4. Infer labels from filenames (the portion before the first dot), or upload
   one label at a time and explicitly set hey_stopmate, unknown, or noise.
5. Confirm the three class names and the counts above before training.
6. Use 16 kHz sampling and a 2,000 ms window for the initial experiment.

CONTENTS AND REPRODUCIBILITY
Speech was synthesized locally, without paid APIs, using Hugging Face's
hexgrad/Kokoro-82M model (Apache-2.0 weights), pinned revision:
f3ff3571791e39611d31c381e3a41a3af07b4987
https://huggingface.co/hexgrad/Kokoro-82M

Training voices: af_heart, af_bella, af_nicole, af_sarah, af_sky,
                 am_adam, am_michael, am_echo.
Testing voices: af_nova, am_fenrir.
Testing voices are disjoint from training voices. All voices come from the
same TTS model; this is not a test on unseen human speakers.

Positive synthesis text: "Hey, Stop Mate." (pronounced as Hey StopMate).
Each speech clip is synthesized with a sampled speaking speed. Full phrases
are retained, padded at varying offsets, and optionally mixed with quiet
procedural background noise and short echoes. No phrase is cropped to fit.
Negative speech includes similar-sounding phrases, isolated parts of the wake
phrase, ordinary speech, and short transit-related phrases.
Noise is procedural white/pink noise, rumble, hum, wind-like noise, clicks,
tones, and quiet noise. These are approximations, not real field recordings.

manifest.json contains labels, split, voice, text, phonemes, augmentations,
model revision, random seed, and SHA-256 hashes. environment.txt records the
Python environment. Generator: ../../scripts/generate_wake_dataset.py.
The generator refuses to overwrite this dataset.

LIMITATIONS
This is a small bootstrap dataset, not a production-quality wake-word corpus.
Add real INMP441 recordings of multiple speakers, distances, rooms, background
conversations, traffic, and the device's own announcements before deployment.
Evaluate false activations during long recordings as well as missed phrases.
A high score on this synthetic test set does not establish real-world accuracy.
Further augmentations of a clip must remain in that clip's original split.

These files train a future classifier; they do not change the ESP32 firmware
or retrain the currently installed WakeNet model.
