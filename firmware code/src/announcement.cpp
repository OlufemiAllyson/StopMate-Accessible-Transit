#include <Arduino.h>
#include <SD_MMC.h>
#include <driver/i2s.h>
#include <cstring>
#include <cerrno>
#include "announcement.h"
#include "announcement_events.h"
#include "no_buses_audio.h"

// Freenove Audio Converter & Amplifier: VCC -> 5V, GND -> GND.
// SCK is unconnected. Speaker: red -> L+, black -> L-.
// These GPIOs must not be shared with an active camera.
constexpr int I2S_BCLK = 26;
constexpr int I2S_DOUT = 22;
constexpr int I2S_LRC = 25;
constexpr i2s_port_t AUDIO_PORT = I2S_NUM_0;
constexpr uint32_t SAMPLE_RATE = 16000;
constexpr int VOLUME_PERCENT = 50;  // Scale PCM samples to 50% amplitude.
constexpr size_t BLOCK_FRAMES = 256;
constexpr uint32_t GAP_MS = 40;
// Freenove onboard slot: 1-bit mode leaves GPIO 12 available for the button.
constexpr int SD_CLK = 14;
constexpr int SD_CMD = 15;
constexpr int SD_D0 = 2;
constexpr uint8_t SD_MAX_OPEN_FILES = 8;  // Up to seven clips validated together.
static_assert(VOLUME_PERCENT >= 0 && VOLUME_PERCENT <= 100, "Invalid volume");

struct Clip {
  File file;
  uint32_t pcmOffset = 0;
  uint32_t pcmBytes = 0;
};
bool audioReady = false;
constexpr const char *NO_BUSES_PATH = "/phrases/no_buses_available.wav";

bool installNoBusesClip() {
  // Preserve an existing user-supplied clip; only install when absent.
  if (SD_MMC.exists(NO_BUSES_PATH)) return true;
  const char *temporary = "/phrases/no_buses_install.tmp";
  if (!SD_MMC.exists("/phrases") && !SD_MMC.mkdir("/phrases")) { Serial.printf("SD mkdir failed: %d\n", errno); return false; }
  // A previous interrupted installation may have left the temporary file.
  if (SD_MMC.exists(temporary) && !SD_MMC.remove(temporary)) { Serial.printf("SD temporary removal failed: %d\n", errno); return false; }
  File file = SD_MMC.open(temporary, FILE_WRITE);
  if (!file) { Serial.printf("SD temporary open failed: %d\n", errno); return false; }
  size_t written = 0;
  uint8_t writeBuffer[512];
  while (written < sizeof(NO_BUSES_WAV)) {
    const size_t count = min(sizeof(writeBuffer), sizeof(NO_BUSES_WAV) - written);
    memcpy(writeBuffer, NO_BUSES_WAV + written, count);
    const size_t bytes = file.write(writeBuffer, count);
    written += bytes;
    if (bytes != count) break;
  }
  file.flush();
  file.close();
  File check = SD_MMC.open(temporary, FILE_READ);
  bool valid = check && written == sizeof(NO_BUSES_WAV) && check.size() == sizeof(NO_BUSES_WAV);
  uint8_t buffer[256];
  for (size_t offset = 0; valid && offset < sizeof(NO_BUSES_WAV);) {
    const size_t count = min(sizeof(buffer), sizeof(NO_BUSES_WAV) - offset);
    valid = check.read(buffer, count) == count && !memcmp(buffer, NO_BUSES_WAV + offset, count);
    offset += count;
  }
  check.close();
  if (!valid || !SD_MMC.rename(temporary, NO_BUSES_PATH)) {
    Serial.printf("SD install failed: written=%u expected=%u verified=%d errno=%d\n", static_cast<unsigned>(written), static_cast<unsigned>(sizeof(NO_BUSES_WAV)), valid, errno);
    SD_MMC.remove(temporary);
    return false;
  }
  Serial.println("Installed no-buses clip on SD card.");
  return true;
}

uint16_t readLE16(const uint8_t *p) {
  return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
uint32_t readLE32(const uint8_t *p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
         (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// Find WAV chunks rather than assuming every WAV has a 44-byte header.
// Validate all clips before playing any part of an announcement.
bool openClip(Clip &clip, const char *path) {
  clip.file = SD_MMC.open(path, FILE_READ);
  uint8_t header[12];
  if (!clip.file || clip.file.read(header, sizeof(header)) != sizeof(header) ||
      memcmp(header, "RIFF", 4) || memcmp(header + 8, "WAVE", 4)) {
    return false;
  }
  const uint64_t riffEnd = uint64_t(readLE32(header + 4)) + 8;
  if (riffEnd < 12 || riffEnd > clip.file.size()) return false;
  bool validFormat = false;
  bool foundData = false;
  while (uint64_t(clip.file.position()) + 8 <= riffEnd) {
    uint8_t chunk[8];
    if (clip.file.read(chunk, sizeof(chunk)) != sizeof(chunk)) return false;
    const uint32_t size = readLE32(chunk + 4);
    const uint32_t start = clip.file.position();
    const uint64_t next = uint64_t(start) + size + (size & 1);
    if (next > riffEnd) return false;
    if (memcmp(chunk, "fmt ", 4) == 0) {
      uint8_t format[16];
      if (size < sizeof(format) ||
          clip.file.read(format, sizeof(format)) != sizeof(format)) return false;
      validFormat = readLE16(format) == 1 && readLE16(format + 2) == 1 &&
                    readLE32(format + 4) == SAMPLE_RATE &&
                    readLE32(format + 8) == SAMPLE_RATE * 2 &&
                    readLE16(format + 12) == 2 && readLE16(format + 14) == 16;
      if (!validFormat) return false;
    } else if (memcmp(chunk, "data", 4) == 0 && !foundData) {
      if (size == 0 || (size & 1)) return false;
      clip.pcmOffset = start;
      clip.pcmBytes = size;
      foundData = true;
    }
    if (!clip.file.seek(static_cast<uint32_t>(next))) return false;
  }
  return validFormat && foundData;
}

bool writeFrames(const int16_t *stereo, size_t frames) {
  const uint8_t *data = reinterpret_cast<const uint8_t *>(stereo);
  size_t remaining = frames * 2 * sizeof(int16_t);
  while (remaining) {
    size_t written = 0;
    const esp_err_t result = i2s_write(AUDIO_PORT, data, remaining, &written,
                                     pdMS_TO_TICKS(1000));
    if (result != ESP_OK || written == 0) {
      Serial.printf("I2S write failed: %s (%u bytes)\n",
                    esp_err_to_name(result), static_cast<unsigned>(written));
      return false;
    }
    data += written;
    remaining -= written;
  }
  return true;
}

bool playSilence(uint32_t milliseconds) {
  const int16_t silence[BLOCK_FRAMES * 2] = {};
  uint32_t remaining = SAMPLE_RATE * milliseconds / 1000;
  while (remaining) {
    const size_t count = min(remaining, static_cast<uint32_t>(BLOCK_FRAMES));
    if (!writeFrames(silence, count)) return false;
    remaining -= count;
  }
  return true;
}

bool playClip(Clip &clip) {
  if (!clip.file.seek(clip.pcmOffset)) return false;
  uint8_t mono[BLOCK_FRAMES * 2];
  int16_t stereo[BLOCK_FRAMES * 2];
  uint32_t remaining = clip.pcmBytes;
  while (remaining) {
    const size_t bytes = min(remaining, static_cast<uint32_t>(sizeof(mono)));
    if (clip.file.read(mono, bytes) != bytes) return false;
    const size_t frames = bytes / 2;
    for (size_t i = 0; i < frames; ++i) {
      const int16_t input = static_cast<int16_t>(readLE16(mono + 2 * i));
      const int16_t quiet = static_cast<int32_t>(input) * VOLUME_PERCENT / 100;
      stereo[2 * i] = quiet;
      stereo[2 * i + 1] = quiet;
    }
    if (!writeFrames(stereo, frames)) return false;
    remaining -= bytes;
  }
  return true;
}

void stopAudio() {
  i2s_zero_dma_buffer(AUDIO_PORT);
  i2s_driver_uninstall(AUDIO_PORT);
  audioReady = false;
  Serial.println("Audio stopped after an error. Reset to retry.");
}

bool playNoBusesAvailable() {
  if (!audioReady) return false;
  Clip clip;
  if (!openClip(clip, NO_BUSES_PATH)) {
    Serial.printf("Missing/invalid no-buses clip: %s\n", NO_BUSES_PATH);
    return false;
  }
  reportNoBusesPlayback("started");
  if (!playClip(clip) || !playSilence(200)) {
    reportNoBusesPlayback("failed");
    stopAudio();
    return false;
  }
  Serial.println("No buses available announcement complete.");
  reportNoBusesPlayback("completed");
  return true;
}

// Blocking playback: call after setup, from one task at a time.
// Returns false for unsupported inputs, unavailable files, or playback errors.
bool playAnnouncement(int routeNumber, int minutes, Route route) {
  if (!audioReady) {
    Serial.println("Audio is not initialized.");
    return false;
  }
  const bool hasNumber = (routeNumber >= 1 && routeNumber <= 60) ||
                        routeNumber == 75 || routeNumber == 76 ||
                        routeNumber == 118 || routeNumber == 126;
  if (!hasNumber || minutes < 0 || minutes > 60) {
    Serial.println("Unsupported input: bus number must be 1-60, 75, 76, 118, or 126; minutes must be 0-60 (0 = arriving now).");
    return false;
  }

  const char *routePath;
  const char *routeName;
  switch (route) {
    case Route::ButlerPlazaToHub:
      routePath = "/routes/butler_hub.wav";
      routeName = "Butler Plaza to the Hub";
      break;
    case Route::ReitzUnionToLivApartments:
      routePath = "/routes/reitz_liv.wav";
      routeName = "Reitz Union to Liv Apartments";
      break;
    case Route::UFHealthShands:
      routePath = "/routes/uf_health_shands.wav";
      routeName = "UF Health Shands";
      break;
    case Route::CottageGrove:
      routePath = "/routes/cottage_grove.wav";
      routeName = "Cottage Grove";
      break;
    default:
      Serial.println("Unsupported route enum.");
      return false;
  }

  char numberPath[24];
  char minutesPath[24];
  snprintf(numberPath, sizeof(numberPath), "/numbers/%03d.wav", routeNumber);
  snprintf(minutesPath, sizeof(minutesPath), "/numbers/%03d.wav", minutes);
  const char *const paths[] = {
      "/phrases/bus.wav", numberPath, "/phrases/for_route.wav", routePath,
      minutes == 0 ? "/phrases/arriving_now.wav" : "/phrases/will_arrive_in.wav", minutesPath,
      minutes == 1 ? "/phrases/minute.wav" : "/phrases/minutes.wav",
  };
  constexpr size_t capacity = sizeof(paths) / sizeof(paths[0]);
  const size_t count = minutes == 0 ? 5 : capacity;
  // Local File objects close automatically on every return path.
  Clip clips[capacity];
  for (size_t i = 0; i < count; ++i) {
    if (!openClip(clips[i], paths[i])) {
      Serial.printf("Missing/invalid 16 kHz mono PCM16 WAV: %s\n", paths[i]);
      return false;
    }
  }

  if (minutes == 0) {
    Serial.printf("Bus %d for route %s is arriving now.\n", routeNumber, routeName);
  } else {
    Serial.printf("Bus %d for route %s will arrive in %d %s.\n",
                  routeNumber, routeName, minutes, minutes == 1 ? "minute" : "minutes");
  }
  reportArrivalPlayback("started", routeNumber, minutes, routeName);
  for (size_t i = 0; i < count; ++i) {
    if (!playClip(clips[i])) {
      reportArrivalPlayback("failed", routeNumber, minutes, routeName);
      Serial.printf("Playback failed: %s\n", paths[i]);
      stopAudio();
      return false;
    }
    if (i + 1 < count && !playSilence(GAP_MS)) {
      reportArrivalPlayback("failed", routeNumber, minutes, routeName);
      stopAudio();
      return false;
    }
  }
  // More than one DMA queue of silence drains the final audio before 'Done'.
  if (!playSilence(200)) {
    reportArrivalPlayback("failed", routeNumber, minutes, routeName);
    stopAudio();
    return false;
  }
  Serial.println("Announcement complete.");
  reportArrivalPlayback("completed", routeNumber, minutes, routeName);
  return true;
}

bool startAudio() {
  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
  config.sample_rate = SAMPLE_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.dma_buf_count = 8;
  config.dma_buf_len = BLOCK_FRAMES;
  config.tx_desc_auto_clear = true;
  esp_err_t result = i2s_driver_install(AUDIO_PORT, &config, 0, nullptr);
  if (result != ESP_OK) {
    Serial.printf("I2S initialization failed: %s\n", esp_err_to_name(result));
    return false;
  }
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = I2S_BCLK;
  pins.ws_io_num = I2S_LRC;
  pins.data_out_num = I2S_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  result = i2s_set_pin(AUDIO_PORT, &pins);
  if (result != ESP_OK) {
    Serial.printf("I2S pin setup failed: %s\n", esp_err_to_name(result));
    i2s_driver_uninstall(AUDIO_PORT);
    return false;
  }
  return true;
}

bool beginAudio() {
  if (!SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0)) {
    Serial.println("Could not configure onboard SDMMC pins.");
    return false;
  }
  // Never format the user's card. Use the board's 1-bit bus at 20 MHz.
  if (!SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT, SD_MAX_OPEN_FILES)) {
    Serial.println("SD mount failed. Insert a FAT-formatted card in the onboard slot, then reset.");
    return false;
  }
  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("No SD card detected. Insert the card, then reset.");
    SD_MMC.end();
    return false;
  }
  Serial.printf("SD card mounted: %llu MiB. Audio source: SD card.\n",
                static_cast<unsigned long long>(SD_MMC.cardSize() / (1024 * 1024)));
  if (!installNoBusesClip()) {
    Serial.println("Could not install no-buses clip on SD card; arrival playback remains available.");
  }
  audioReady = startAudio();
  if (!audioReady) SD_MMC.end();
  return audioReady;
}

void idleAudio() {
  if (audioReady) {
    if (!playSilence(20)) stopAudio();
  } else {
    delay(1000);
  }
}
