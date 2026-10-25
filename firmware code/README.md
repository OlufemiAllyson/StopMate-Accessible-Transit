# Stopmate arrival announcements

The ESP32 starts Wi-Fi at boot and keeps it connected during idle listening,
recording, and announcement playback. It automatically reconnects after a drop;
the main loop also retries failed connections every 30 seconds without blocking
voice recognition. Clock synchronization for verified HTTPS starts on connection.
On each ultrasonic hand wave, joystick button press, recognized **Hey StopMate** wake phrase, or Serial
`schedule` command,
it requests one arrival snapshot for RTS stop **0440 — The Standard Apartments**
using the existing connection. It announces the nearest supported inbound bus
for each configured route, ordered by arrival time. There is no automatic bus
polling, API retry, or demo playback; reconnecting Wi-Fi does not request arrivals.
Startup is silent: Wi-Fi and recognition start, the LED ring is cleared, and no
bus lookup or announcement runs until explicitly requested.

Connect joystick **SW to GPIO 12** and **GND to GND**. The button is active-low
with an internal pull-up. Both press and release must be stable for **40 ms**.
Holding it triggers only once. Network lookup and playback are blocking, so
presses during that work are ignored; release the button after playback before
pressing again. Each accepted press fetches fresh arrivals.

### Ultrasonic wave trigger

Connect an HC-SR04: **VCC to 5V, GND to GND, TRIG to GPIO18**.
Connect **ECHO through 1 kΩ to GPIO19**, and connect **2 kΩ from GPIO19 to GND**.
This divider reduces the sensor's 5V echo to about 3.3V; do not connect ECHO
directly to the ESP32. These pins assume the camera remains disabled.

Clear the sensing area, then wave your hand **2-30 cm** in front of the sensor.
Two consecutive near readings (sampled every 70 ms) trigger the same fresh
arrival lookup, speech, and ring update as the button. The detector recognizes
an approach, not the direction of a gesture. It requires at least 300 ms of
clear readings (40+ cm or no echo) before arming, including at startup.
A held hand does not repeat. After any announcement or blocking Serial operation,
it waits three seconds and requires fresh clearance; waves during those operations
are ignored. An object permanently within 40 cm can prevent rearming.
Each echo wait is capped at 15 ms; voice capture continues on its worker task.
Pin/timing settings are in `src/ultrasonic_trigger.cpp`; distance, debounce, and
cooldown settings are in `include/wave_detector.h`.

Current destinations are route **8 → UF Health Shands** and route
**13 → Cottage Grove**. Arrival times come from the official RTS BusTime v3 API:
https://riderts.app/bustime/apidoc/docs/DeveloperAPIGuide3_0.pdf

## Configure and upload

Copy `include/secrets.example.h` to `include/secrets.h`, then fill in
`WIFI_SSID`, `WIFI_PASSWORD`, and `RTS_API_KEY`. Use a 2.4 GHz network.
Leave the password empty only for an open network. Do not commit `secrets.h`.

Run from the project directory in a PlatformIO terminal:

```sh
pio run
pio run -t upload
pio device monitor -b 115200
```

The build prepares `.pio/audio_data`. Copy its `numbers/`, `phrases/`, and
`routes/` folders to the root of a FAT-formatted SD card. Safely eject the card,
insert it in the onboard slot, and reset the ESP32. Firmware and credential
changes need a firmware upload; audio changes require copying the updated
folders to the card. Do not use `uploadfs` for SD audio.
Close Serial Monitor before uploading. If `pio` is not on your PATH, use
`~/.platformio/penv/bin/pio`. The build also requires `ffmpeg`.

Connection, clock, HTTP, JSON, and audio failures are reported in Serial Monitor
without printing credentials. Missing Wi-Fi settings cause a single diagnostic
and no request. After a network outage, Wi-Fi reconnects automatically; press the
button or say the wake phrase for a fresh lookup. Credential changes still need
a firmware upload. TLS uses a
verified USERTrust root certificate in `include/rts_root_ca.h`; verification is
never disabled. The request does not follow redirects or log the key-bearing URL.

## Announcement behavior

The snapshot selects one nearest prediction per configured route and direction.
Only matching stop IDs and arrival predictions are used; departures, unknown
routes/directions, delayed predictions, malformed countdowns, and times above
24 hours are skipped. `DUE` or zero uses the recording “is arriving now.”
Times from 1 through 60 minutes use number recordings and the singular/plural
minute phrase. Longer predictions appear in the ring's 60+ position but are not
spoken, because the existing minute snippets support only 0-60.
Elapsed download/playback time is subtracted from numeric estimates, rounding
remaining seconds up to whole minutes. Snapshots older than two minutes are
not played. A confirmed empty prediction list or RTS `No service scheduled` response plays
`/phrases/no_buses_available.wav`. Network failures, malformed data, other API
errors, and lists containing only skipped predictions also display and play the
same no-buses fallback, as requested. Serial retains the actual failure reason;
in these cases the fallback does not establish that service is absent.

Audio remains at **50% PCM amplitude**, with 40 ms between snippets. Change
`VOLUME_PERCENT` in `src/announcement.cpp` if needed. Playback blocks until the
announcement finishes. `announceArrivals()` performs one lookup per call;
`src/main.cpp` calls it on a wave, button, wake-phrase, or Serial schedule request.

## Arrival LED ring

Connect the Freenove eight-WS2812 module's **IN S to GPIO 21, V to 5V, and G to
GND**. Leave OUT unconnected. Adafruit NeoPixel 1.15.5 uses the ESP32 RMT
peripheral, leaving the speaker and microphone I2S ports available.

Rotate the ring so its first LED (D1, library index 0) is at the top. Positions
follow the module's LED wiring order; `FIRST_LED` and `REVERSE_ORDER` in
`src/arrival_ring.cpp` can adapt it to another orientation.

| Position | Arrival time at the last request |
| --- | --- |
| D1 | Arriving now / DUE |
| D2 | 1-9 minutes |
| D3 | 10-19 minutes |
| D4 | 20-29 minutes |
| D5 | 30-39 minutes |
| D6 | 40-49 minutes |
| D7 | 50-59 minutes |
| D8 | 60+ minutes |

The nearest arrival of each configured route gets one position. Route colors
come from RTS BusTime `getroutes`'s `rtclr`, verified on 2026-10-24: route 8 is
orange `#FF7B00`; route 13 is blue-gray `#49535A`. They are stored alongside
the route definitions in `include/route_config.h`. LEDs use about 25% brightness.

Two or more routes in one time range alternate their official colors every
second. A small display task keeps that animation running during playback;
it never requests predictions or reduces ETAs. Positions remain frozen until
the next explicit schedule request. The ring clears at the start of a request,
so a failed lookup cannot leave an old estimate displayed. Confirmed no buses
also leaves the ring dark and plays the existing no-buses audio.

Wave, use the button, say **Hey StopMate**, or send `schedule` at 115200 baud to update
both the ring and announcements. Resetting leaves the ring dark and silent.

## LCD1602 arrival display

Connect the I2C LCD1602 backpack's **SDA to GPIO23** and **SCL/SCK to GPIO13**,
with a common GND. Use the supply voltage required by your LCD module. A typical
5V LCD1602 backpack needs a bidirectional I2C level shifter between its 5V SDA/SCL
and the ESP32's 3.3V pins; do not expose ESP32 GPIOs to 5V pull-ups.
GPIO13 is available with the current 1-bit SD configuration; keep the camera disabled.

The hd44780 1.3.2 library automatically detects the backpack address, using the
standard Freenove/PCF8574 mapping (RS0, RW1, EN2, backlight3, data4-7).
Initialization failures are logged and
leave the other triggers, speech, and LEDs working. If the backlight is on but
characters are invisible, adjust the backpack's contrast potentiometer.

At startup the screen shows `StopMate 0440` / `Wave for buses`, without fetching
or speaking. A wave, button, wake phrase, or Serial `schedule` request shows
`Checking buses`, then the same arrival snapshot used by the LED ring:

```text
Bus 8: 6 min
UF Health Shands
```

The screen cycles through routes with available predictions every **3 seconds**,
in arrival order. One available route stays displayed. Zero minutes shows `DUE`.
An empty valid result or failed lookup shows `No buses` / `available` and plays
the existing no-buses audio. Serial retains lookup failure diagnostics. Values remain
frozen until the next request, just like the LEDs. No automatic API polling is added.
A dedicated display task keeps cycling during network/audio/recording work;
only changed frames are written, with padded lines to erase old characters.

Add destinations through `displayName` in `include/route_config.h` (up to 16
characters; longer names are truncated). Set pins and the cycling interval in
`src/arrival_display.cpp`; layout and status text live in `src/arrival_display_model.cpp`.

## Local web companion

The matching GitHub repository's `server/` app mirrors actual device announcements
over USB Serial. Start it with `npm run local` from that folder (Node.js 22+ and
Python with pyserial are required), then open http://127.0.0.1:3000. Close Serial
Monitor and the recording downloader first: only one process can own the USB port.
The ESP32 keeps using Wi-Fi for RTS; this local web connection does not require
the computer to be on the same Wi-Fi network.

`src/announcement_events.cpp` emits versioned `STOPMATE_EVENT` JSON lines at
request start/end and audio start/completion/failure, plus ten-second idle
heartbeats. Route events use the actual spoken minutes and destination after
audio files are validated. No-buses playback (including the requested error
fallback) is mirrored too. Unplayed predictions are not reported as spoken.
The browser keeps the latest request's announcements, with their original times
and timestamp; it does not fetch an independent schedule or show mock arrivals.

No new API keys or Wi-Fi credentials are sent over this protocol. Normal boot
remains silent. USB disconnects do not interrupt standalone device announcements,
but the website needs the USB bridge running to receive them. The server marks
the device disconnected after 90 seconds without events. See `server/README.md`
in the repository for local startup, event schema, and verification instructions.

## Local voice trigger

Say **Hey StopMate** after Serial prints `Voice ready`. The firmware now uses
Edge Impulse project `eeellls-project-1`, project ID 1147459, impulse 5,
deployment version 2, from `eeellls-project-1-cpp-mcu-v2-impulse-#5/`.
`scripts/build_edge_impulse.py` compiles the exported model and SDK directly;
keep that directory in the project. The export is preserved without modifications.
The previous WakeNet dependency remains on disk but is excluded from the build.
No microphone audio is uploaded; networking is used only for RTS arrivals.

The exported INT8 model has labels `hey_stopmate` and `noise` (no separate `unknown` class),
and expects **one second** of 16 kHz audio, not a two-second window. Continuous
inference consumes 250 ms slices. It waits for a complete window and requires
`hey_stopmate >= 0.90`, a 0.20 lead over the other labels, and two consecutive
matching windows. Tune `VOICE_WAKE_THRESHOLD` and `VOICE_WAKE_MARGIN` in
`include/voice_model_config.h` after testing real speech and background noise.
If it misses slowly spoken phrases, retrain/export with a longer window;
the firmware must follow the model's exported dimensions.

| INMP441 | ESP32 |
| --- | --- |
| VDD | 3.3V |
| GND | GND |
| SCK | GPIO 27 |
| WS | GPIO 32 |
| SD | GPIO 33 |
| L/R | 3.3V (right channel) |

`src/voice_trigger.cpp` captures 16 kHz right-channel audio on I2S port 1;
playback remains on port 0. It converts the microphone's 32-bit slots to signed
PCM16 with saturating 4x gain. A worker task runs detection and signals the main
loop, which fetches and plays arrivals. Detection pauses during network lookup
and playback, clears its state, and discards input for 1.5 seconds on resuming.
The button keeps its existing debounce behavior. Voice initialization failures
are reported on Serial and leave the button available.

Serial reports microphone peak level and read/overflow errors every ten seconds.
Scores above 0.5 include preprocessing and inference timings. DMA overflow
invalidates the streaming classifier state rather than joining discontinuous audio. A
persistent zero peak suggests a wiring/channel problem; repeated full-scale
peaks suggest clipping or bad wiring. These diagnostics do not establish that
the wake phrase is recognized reliably in the installation; test it aloud at
the intended distance. The camera must remain disabled with this pin mapping.

## Record training audio over Serial

Open PlatformIO's Serial Monitor at **115200 baud** after startup finishes.
Send a command followed by Enter (LF, CR, and CRLF are supported):

```text
help
record hey_stopmate
record unknown
record noise 5
```

`record <label> [seconds]` records once. Labels are `hey_stopmate`, `unknown`,
and `noise`; duration defaults to 2 seconds and accepts whole numbers from 1
through 10. After the three-second countdown, wait for **Recording now. Speak
now.** Say the wake phrase for `hey_stopmate`, unrelated speech for `unknown`,
or capture background sound without intentional speech for `noise`.

Files are saved as `/recordings/hey_stopmate_001.wav`, etc., on the SD card.
Each label has its own sequence; existing WAV files and unfinished temporary
files are skipped, including after reboot. The format is 16 kHz mono PCM16,
using exactly the recognition microphone's conversion and gain. Two seconds
after the cue plus 300 ms of pre-roll occupy 73,644 bytes (a 2.3-second WAV).
The microphone warms up before the countdown and stays running through it.
The last 300 ms before the cue are saved, and capture continues uninterrupted
across the cue so early speech is preserved. Nothing is discarded at the cue.
The requested duration is the additional audio after the cue. Serial reports
captured sample count, capture time, peak level, and clipped sample count so you
can identify silent or distorted recordings.

Recognition and announcements pause during countdown, capture, and saving.
Capture goes into RAM (PSRAM when available), then the WAV is written to SD,
read back to verify it, and renamed from a temporary file. Capture errors
discard the clip. Recognition automatically resumes afterward; the button
requires a fresh release/press. Commands are processed when the main loop is
idle, so an ongoing arrival lookup/playback finishes first.

Wait for **Saved** before switching off the board and removing the SD card.
Copy the WAVs to your computer and upload them to Edge Impulse with their
corresponding labels. Listen to a few first and keep separate recording
sessions/speakers for evaluation. This feature collects data; it does not
retrain or replace the current one-second model.

To copy recordings over USB without removing the card, close Serial Monitor
and run:

```sh
python3 scripts/download_recordings.py
```

Files are copied into the project's `recordings/` directory. Use `--file
hey_stopmate_001.wav` for one file, `--output <directory>` for another destination,
or `--port <device>` for another USB port. The script resets the board, waits for
startup, verifies the transfer checksum and WAV format, and preserves SD files.
It refuses to overwrite a different local file. Serial commands `recordings`
and `download <filename.wav>` provide the listing and transfer protocol;
recognition pauses during each SD operation and then resumes.

## Add another route

1. Add a 24 kHz mono 16-bit PCM WAV destination recording under
   `kokoro_transit_snippets/routes/`. Filenames must fit the project's 31-character
   limit, or have a short alias in `scripts/prepare_audio.py`.
2. Add its `Route` enum value in `include/announcement.h`, and its staged file
   path and display name in `playAnnouncement()` in `src/announcement.cpp`.
3. Add an API route ID, direction, spoken bus number, audio enum, RTS RGB color, and LCD destination name to
   `MONITORED_ROUTES` in `include/route_config.h`. This table controls which
   predictions become announcements. Change `RTS_STOP_ID` there for another stop.
4. Ensure its bus number recording exists and is allowed by the number validation
   in `playAnnouncement()`. Existing numbers are 1–60, 75, 76, 118, and 126.
5. Upload firmware and copy the rebuilt audio folders to the SD card.

For example, after `beginAudio()` succeeds:

```cpp
playAnnouncement(8, 5, Route::UFHealthShands);
playAnnouncement(13, 1, Route::CottageGrove);
playAnnouncement(8, 0, Route::UFHealthShands);  // is arriving now
```

`src/main.cpp` controls startup; `src/rts_client.cpp` handles networking and
announcing the snapshot; `src/rts_predictions.cpp` filters, deduplicates, and
sorts predictions; `src/announcement.cpp` handles WAV parsing and I2S playback.
The original Butler and Reitz audio enums remain available for direct calls.

## Flash storage and wiring

`partitions.csv` allocates 3.875 MiB to the application on the 4 MiB flash chip,
with USB updates and no OTA slot. Edge Impulse model weights are linked into the
firmware; the former LittleFS audio partition has been removed.
Playback reads exclusively from the onboard SD card using 1-bit SDMMC
(CLK GPIO 14, CMD GPIO 15, D0 GPIO 2), leaving GPIO 12 for the button.
This follows [Freenove's SDMMC example](https://github.com/Freenove/Freenove_ESP32_WROVER_Board/blob/main/C/Sketches/Sketch_03.1_SDMMC_Test/Sketch_03.1_SDMMC_Test.ino).
The mount allows eight open files because announcements validate up to seven
clips together. Insert the card before startup; hot swapping is not supported.
The build stages all 75
reusable recordings in `.pio/audio_data`, converting source speech to **16 kHz,
mono PCM16** for the existing playback format. The original 24 kHz recordings remain
unchanged, and example WAV files are excluded. Never edit the generated copies.
The new `phrases/arriving_now.wav` supplies the imminent-arrival announcement.

| Audio module | ESP32 |
| --- | --- |
| BCK | GPIO 26 |
| DIN | GPIO 22 |
| LCK | GPIO 25 |
| VCC | 5V |
| GND | GND |
| SCK | Unconnected |

Connect the speaker to L+ and L−. Do not enable the camera with this pin mapping.
Mount failures never format the SD card. A missing card or failed mount is
reported on Serial; insert the card and reset to retry. Missing or invalid clips
are reported by path before any part of that announcement plays.

## Host tests

After installing PlatformIO dependencies, run:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Iinclude -I.pio/libdeps/freenove_esp32_wrover/ArduinoJson/src \
  test/test_rts_predictions.cpp src/rts_predictions.cpp -o .pio/test_rts_predictions
.pio/test_rts_predictions
```

These exercise earliest-arrival selection, route/direction/stop filtering,
deduplication, ordering, DUE handling, delayed vehicles, malformed countdowns,
and empty results without making API calls.

To test LED time boundaries, route-color collisions, and frozen snapshots:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Iinclude -I.pio/libdeps/freenove_esp32_wrover/ArduinoJson/src \
  test/test_arrival_ring.cpp src/arrival_ring_model.cpp -o .pio/test_arrival_ring
.pio/test_arrival_ring
```

To replay the 40 synthetic held-out clips plus silence through the exported
continuous classifier, run `python3 scripts/check_voice_model.py` (requires
CMake and a C++ compiler). Results for deployment 2 go to `.pio/ei_host_v2/summary.json` and
`results.log`. This tests model behavior, not ESP32 processing time or live-mic
accuracy. In the earlier replay with a 0.80 threshold and two-window rule, deployment 2
(impulse 5) triggered on 19/20 positives and 8/21 negatives/silence. The previous
deployment triggered on 5/20 positives and 1/21 negatives/silence. This new export
detects more wake phrases in this replay but also falsely triggers more often;
these results do not establish live microphone accuracy or independent test-set
performance for the user's new training run. It has no separate unknown-speech
class. Evaluate real unrelated speech and background noise before tuning it.
The current firmware uses a stricter 0.90 threshold; the figures above describe
the earlier 0.80 setting. The original test split should not be repeatedly used
for threshold tuning.

The MCU build enables ESP-DSP FFT acceleration and `-O3`; microphone software
buffers use PSRAM when available. Serial's maximum processing time should stay
below the 250 ms slice interval, with zero capture overflow errors.

The user-supplied no-buses snippet is staged as 16 kHz mono PCM16. The firmware
carries a small installation copy and writes it to the SD card only if
`/phrases/no_buses_available.wav` is absent. It verifies the temporary file before
renaming it and preserves existing audio. Playback uses SD at the same 50% volume.
This response applies to button, voice-triggered, and Serial schedule lookups.

### Selecting the stop from the local website

The website's **Choose a stop → Switch stop & announce** control sends a USB
command to the ESP32. Firmware validates the stop against `supported_stops.h`,
saves it in NVS, confirms it to the website, and requests fresh arrivals. The
selection survives restarts, and startup remains silent. All button, wave, and
voice requests use this selected stop. The default for a new device is 0440.

The current catalog covers 60 stops serving inbound routes 8 and 13, matching
the destination audio in `route_config.h`. Other routes at those stops are not
announced. To expand coverage, add the matching route/direction and audio first,
then regenerate both the server stop catalog and firmware allowlist with
`server/scripts/refresh-stops.py`. Upload matching firmware and restart the server.

The serial control syntax is `stop <four-digit-ID> <command-UUID>`; the USB bridge
manages IDs, acknowledgments, and retries. Repeating the same command ID during
a boot acknowledges it without replaying audio.
