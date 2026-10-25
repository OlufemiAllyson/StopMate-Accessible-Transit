#include "serial_recorder.h"
#include "voice_trigger.h"
#include "selected_stop.h"
#include "announcement_events.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <cstring>
#include <cstdlib>

namespace {
constexpr uint32_t RATE = 16000;
char command[80];
size_t commandLength = 0;
bool tooLong = false;
bool scheduleRequested = false;

void put16(uint8_t *p, uint16_t value) {
  p[0] = value;
  p[1] = value >> 8;
}

void put32(uint8_t *p, uint32_t value) {
  put16(p, value);
  put16(p + 2, value >> 16);
}

bool saveRecording(const char *path, const char *temporary, const int16_t *samples, size_t count) {
  uint8_t header[44] = {};
  memcpy(header, "RIFF", 4);
  put32(header + 4, 36 + count * 2);
  memcpy(header + 8, "WAVEfmt ", 8);
  put32(header + 16, 16);
  put16(header + 20, 1);  // PCM
  put16(header + 22, 1);  // Mono
  put32(header + 24, RATE);
  put32(header + 28, RATE * 2);
  put16(header + 32, 2);
  put16(header + 34, 16);
  memcpy(header + 36, "data", 4);
  put32(header + 40, count * 2);
  File file = SD_MMC.open(temporary, FILE_WRITE);
  if (!file) return false;
  bool ok = file.write(header, sizeof(header)) == sizeof(header);
  uint8_t block[512];
  for (size_t offset = 0; offset < count && ok;) {
    const size_t frames = min(static_cast<size_t>(256), count - offset);
    for (size_t i = 0; i < frames; ++i) put16(block + i * 2, samples[offset + i]);
    ok = file.write(block, frames * 2) == frames * 2;
    offset += frames;
  }
  file.flush();
  file.close();
  // Read back before publishing the file; discard incomplete/corrupt writes.
  if (ok) {
    file = SD_MMC.open(temporary, FILE_READ);
    ok = file && file.size() == sizeof(header) + count * 2;
    if (ok) ok = file.read(block, sizeof(header)) == sizeof(header) &&
                 memcmp(block, header, sizeof(header)) == 0;
    for (size_t offset = 0; offset < count && ok;) {
      const size_t frames = min(static_cast<size_t>(256), count - offset);
      ok = file.read(block, frames * 2) == frames * 2;
      for (size_t i = 0; i < frames && ok; ++i) {
        const uint16_t value = block[i * 2] | (static_cast<uint16_t>(block[i * 2 + 1]) << 8);
        ok = value == static_cast<uint16_t>(samples[offset + i]);
      }
      offset += frames;
    }
    file.close();
  }
  if (ok) ok = !SD_MMC.exists(path) && SD_MMC.rename(temporary, path);
  if (!ok) SD_MMC.remove(temporary);
  return ok;
}

void record(const char *label, unsigned seconds) {
  if (SD_MMC.cardType() == CARD_NONE ||
      (!SD_MMC.exists("/recordings") && !SD_MMC.mkdir("/recordings"))) {
    Serial.println("Recording failed: SD card or recordings directory unavailable.");
    return;
  }
  char path[80], temporary[80];
  bool available = false;
  for (unsigned index = 1; index <= 99999; ++index) {
    snprintf(path, sizeof(path), "/recordings/%s_%03u.wav", label, index);
    snprintf(temporary, sizeof(temporary), "/recordings/%s_%03u.tmp", label, index);
    if (!SD_MMC.exists(path) && !SD_MMC.exists(temporary)) {
      available = true;
      break;
    }
  }
  if (!available) {
    Serial.println("Recording failed: no unused filename.");
    return;
  }
  const size_t count = seconds * RATE + VOICE_RECORDING_PREROLL_SAMPLES;
  int16_t *samples = static_cast<int16_t *>(psramFound() ? ps_malloc(count * 2) : malloc(count * 2));
  if (!samples) {
    Serial.println("Recording failed: insufficient RAM. Try a shorter duration.");
    return;
  }
  pauseVoiceTrigger();
  Serial.printf("Preparing %u-second %s recording. Wait for the countdown and start cue.\n", seconds, label);
  if (capturePausedVoiceAudio(samples, count)) {
    Serial.println("Saving WAV to SD...");
    if (saveRecording(path, temporary, samples, count))
      Serial.printf("Saved %s (%u bytes; 16000 Hz, mono, PCM16).\n", path, static_cast<unsigned>(44 + count * 2));
    else Serial.println("Recording failed: SD write/verification failed. Check card space and connection.");
  }
  free(samples);
  resumeVoiceTrigger();
}

void listRecordings() {
  pauseVoiceTrigger();
  Serial.println("RECORDINGS_BEGIN");
  File directory = SD_MMC.open("/recordings");
  if (directory && directory.isDirectory()) {
    File file = directory.openNextFile();
    while (file) {
      const String name = file.name();
      if (!file.isDirectory() && name.endsWith(".wav"))
        Serial.printf("RECORDING %s %u\n", file.name(), static_cast<unsigned>(file.size()));
      file.close();
      file = directory.openNextFile();
    }
  }
  directory.close();
  Serial.println("RECORDINGS_END");
  resumeVoiceTrigger();
}

void downloadRecording(const char *name) {
  // Restrict reads to simple WAV filenames inside the recordings directory.
  const size_t length = strlen(name);
  if (length < 5 || length > 60 || strcmp(name + length - 4, ".wav")) {
    Serial.println("DOWNLOAD_ERROR invalid filename");
    return;
  }
  for (size_t i = 0; i < length - 4; ++i) {
    const char c = name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_')) {
      Serial.println("DOWNLOAD_ERROR invalid filename");
      return;
    }
  }
  char path[80];
  snprintf(path, sizeof(path), "/recordings/%s", name);
  pauseVoiceTrigger();
  File file = SD_MMC.open(path, FILE_READ);
  if (!file || file.isDirectory()) {
    Serial.println("DOWNLOAD_ERROR file not found");
  } else {
    const size_t size = file.size();
    Serial.printf("DOWNLOAD_BEGIN %s %u\n", name, static_cast<unsigned>(size));
    uint32_t checksum = 2166136261u;  // FNV-1a, checked by the receiver.
    uint8_t bytes[64];
    char hex[129];
    constexpr char DIGITS[] = "0123456789abcdef";
    size_t sent = 0;
    while (sent < size) {
      const size_t wanted = min(sizeof(bytes), size - sent);
      const int got = file.read(bytes, wanted);
      if (got != static_cast<int>(wanted)) break;
      for (size_t i = 0; i < wanted; ++i) {
        hex[i * 2] = DIGITS[bytes[i] >> 4];
        hex[i * 2 + 1] = DIGITS[bytes[i] & 15];
        checksum = (checksum ^ bytes[i]) * 16777619u;
      }
      hex[wanted * 2] = '\0';
      Serial.printf("DOWNLOAD_DATA %s\n", hex);
      sent += wanted;
      delay(1);
    }
    if (sent == size) Serial.printf("DOWNLOAD_END %08lx\n", static_cast<unsigned long>(checksum));
    else Serial.println("DOWNLOAD_ERROR SD read failed");
  }
  file.close();
  Serial.flush();
  resumeVoiceTrigger();
}

void executeCommand() {
  char *state = nullptr;
  const char *verb = strtok_r(command, " \t", &state);
  if (!verb) return;
  if (!strcmp(verb, "help")) {
    printRecordingHelp();
    return;
  }
  const char *label = strtok_r(nullptr, " \t", &state);
  const char *duration = strtok_r(nullptr, " \t", &state);
  const char *extra = strtok_r(nullptr, " \t", &state);
  if (!strcmp(verb, "stop") && label && duration && !extra) {
    // The bridge retries until acknowledged. Duplicate commands never replay audio.
    static char lastStopCommand[37] = "";
    if (strlen(duration) != 36 || strspn(duration, "0123456789abcdef-") != 36) {
      Serial.println("Invalid stop command ID.");
      return;
    }
    if (!strcmp(lastStopCommand, duration)) {
      reportStopSelected(duration);
      return;
    }
    if (!selectStop(label)) {
      Serial.println("Stop selection failed: unsupported stop or storage unavailable.");
      return;
    }
    strlcpy(lastStopCommand, duration, sizeof(lastStopCommand));
    reportStopSelected(duration);
    scheduleRequested = true;
    return;
  }
  if (!strcmp(verb, "schedule") && !label) {
    scheduleRequested = true;
    return;
  }
  if (!strcmp(verb, "recordings") && !label) {
    listRecordings();
    return;
  }
  if (!strcmp(verb, "download") && label && !duration) {
    downloadRecording(label);
    return;
  }
  if (strcmp(verb, "record") || !label || extra ||
      (strcmp(label, "hey_stopmate") && strcmp(label, "unknown") && strcmp(label, "noise"))) {
    Serial.println("Invalid command. Type help for recording commands.");
    return;
  }
  char *end = nullptr;
  const long seconds = duration ? strtol(duration, &end, 10) : 2;
  if (seconds < 1 || seconds > 10 || (duration && (*end || end == duration))) {
    Serial.println("Duration must be a whole number from 1 to 10 seconds.");
    return;
  }
  record(label, static_cast<unsigned>(seconds));
}
}

void printRecordingHelp() {
  Serial.println("Request arrivals and update LEDs: schedule");
  Serial.println("Serial recording: record <hey_stopmate|unknown|noise> [seconds]");
  Serial.println("Default: 2 seconds; range: 1-10. Example: record hey_stopmate");
  Serial.println("Each WAV also includes 300 ms before the cue to preserve the first syllable.");
  Serial.println("Send with Enter/newline. Type help to repeat. Files: /recordings/<label>_NNN.wav");
  Serial.println("List: recordings. USB transfer: download <filename.wav> (use scripts/download_recordings.py).");
}

bool takeSerialScheduleRequest() {
  const bool requested = scheduleRequested;
  scheduleRequested = false;
  return requested;
}

bool pollRecordingCommands() {
  while (Serial.available()) {
    const char ch = Serial.read();
    if (ch == '\r' || ch == '\n') {
      if (!commandLength && !tooLong) continue;
      command[commandLength] = '\0';
      if (tooLong) Serial.println("Command too long; discarded. Type help.");
      else executeCommand();
      commandLength = 0;
      tooLong = false;
      return true;
    }
    if (ch == '\b' || ch == 127) {
      if (commandLength && !tooLong) --commandLength;
    } else if (commandLength < sizeof(command) - 1 && !tooLong) {
      command[commandLength++] = ch;
    } else {
      tooLong = true;
    }
  }
  return false;
}
