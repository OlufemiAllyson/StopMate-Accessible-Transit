#include "voice_trigger.h"
#include "voice_model_config.h"
#include <Arduino.h>
#include <driver/i2s.h>
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include <cstring>
#include <freertos/semphr.h>
#include <atomic>

namespace {
constexpr i2s_port_t MIC_PORT = I2S_NUM_1;
constexpr uint32_t SAMPLE_RATE = 16000;
constexpr uint32_t COOLDOWN_MS = 1500;
static_assert(EI_CLASSIFIER_FREQUENCY == SAMPLE_RATE, "Model must use 16 kHz audio");
static_assert(EI_CLASSIFIER_SENSOR == EI_CLASSIFIER_SENSOR_MICROPHONE, "Expected audio model");
int wakeLabel = -1;
unsigned slicesSeen = 0;
unsigned hits = 0;
QueueHandle_t micEvents = nullptr;

void resetClassifier() {
  run_classifier_init();
  slicesSeen = 0;
  hits = 0;
}
SemaphoreHandle_t lock = nullptr;
std::atomic<bool> pending{false};
bool ready = false;
bool listening = true;  // Protected by lock.
uint32_t resumedAt = 0;
constexpr int samplesPerFrame = EI_CLASSIFIER_SLICE_SIZE;
int32_t *raw = nullptr;
int16_t *pcm = nullptr;

int16_t microphoneSample(int32_t input) {
  // INMP441's signed 24-bit samples are left-aligned; apply 4x gain.
  return static_cast<int16_t>(constrain(input >> 14, -32768, 32767));
}

// Drain queued data AND a partially consumed DMA buffer. A fixed number of
// samples is not a queue flush: backlog varies with the preceding inference.
bool drainMicrophoneQueue() {
  for (unsigned block = 0; block < 64; ++block) {
    size_t bytes = 0;
    if (i2s_read(MIC_PORT, raw, 256 * sizeof(int32_t), &bytes, 0) != ESP_OK) return false;
    if (bytes == 0) return true;
  }
  return false;
}

bool discardMicrophoneSamples(size_t frames) {
  while (frames) {
    const size_t block = min(frames, static_cast<size_t>(256));
    size_t bytes = 0;
    if (i2s_read(MIC_PORT, raw, block * sizeof(int32_t), &bytes,
                 pdMS_TO_TICKS(1000)) != ESP_OK || bytes != block * sizeof(int32_t)) return false;
    frames -= block;
  }
  return true;
}

void detectTask(void *) {
  uint32_t lastHealth = millis();
  int peak = 0;
  unsigned readErrors = 0;
  int maxProcessingMs = 0;
  while (true) {
    xSemaphoreTake(lock, portMAX_DELAY);
    size_t bytes = 0;
    const size_t wanted = samplesPerFrame * sizeof(int32_t);
    const auto result = i2s_read(MIC_PORT, raw, wanted, &bytes, pdMS_TO_TICKS(500));
    if (result == ESP_OK && bytes == wanted) {
      for (int i = 0; i < samplesPerFrame; ++i) {
        pcm[i] = microphoneSample(raw[i]);
        peak = max(peak, abs(static_cast<int>(pcm[i])));
      }
      bool overrun = false;
      i2s_event_t event;
      while (xQueueReceive(micEvents, &event, 0) == pdTRUE) {
        if (event.type == I2S_EVENT_RX_Q_OVF || event.type == I2S_EVENT_DMA_ERROR) overrun = true;
      }
      if (overrun) {
        ++readErrors;
        resetClassifier();
      } else if (listening && millis() - resumedAt >= COOLDOWN_MS) {
        signal_t signal;
        signal.total_length = samplesPerFrame;
        signal.get_data = [](size_t offset, size_t length, float *out) -> int {
          if (offset + length > static_cast<size_t>(samplesPerFrame)) return -1;
          for (size_t i = 0; i < length; ++i) out[i] = pcm[offset + i];
          return 0;
        };
        ei_impulse_result_t prediction = {};
        const auto status = run_classifier_continuous(&signal, &prediction, false, false);
        if (status != EI_IMPULSE_OK) {
          Serial.printf("Voice inference failed: %d.\n", status);
          resetClassifier();
        } else {
          maxProcessingMs = max(maxProcessingMs, prediction.timing.dsp + prediction.timing.classification);
          ++slicesSeen;
          const float score = prediction.classification[wakeLabel].value;
          float competitor = 0;
          for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; ++i) {
            if (static_cast<int>(i) != wakeLabel) competitor = max(competitor, prediction.classification[i].value);
          }
          // Fill a complete model window before considering scores. Require two
          // consecutive overlapping windows to reject isolated confidence spikes.
          const bool match = slicesSeen >= EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW &&
                             score >= VOICE_WAKE_THRESHOLD && score - competitor >= VOICE_WAKE_MARGIN;
          hits = match ? hits + 1 : 0;
          if (score >= .5f && slicesSeen >= EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW) {
            Serial.printf("Hey StopMate: %.3f, other: %.3f, slice=%u, hits=%u, DSP=%d ms, NN=%d ms.\n",
                          score, competitor, slicesSeen, hits, prediction.timing.dsp, prediction.timing.classification);
          }
          if (hits >= VOICE_REQUIRED_HITS) {
            listening = false;
            pending.store(true);
            Serial.println("Wake phrase detected: Hey StopMate.");
          }
        }
      }
    } else {
      ++readErrors;
      resetClassifier();
    }
    if (millis() - lastHealth >= 10000) {
      Serial.printf("Microphone: peak=%d/32768, read/overflow errors=%u, max processing=%d ms per 250 ms slice.\n", peak, readErrors, maxProcessingMs);
      peak = 0;
      readErrors = 0;
      maxProcessingMs = 0;
      lastHealth = millis();
    }
    xSemaphoreGive(lock);
    vTaskDelay(1);
  }
}
}

bool beginVoiceTrigger() {
  if (ready) return true;
  Serial.printf("Voice model: %s, deployment %d, %d labels, %d samples/window.\n",
                EI_CLASSIFIER_PROJECT_NAME, EI_CLASSIFIER_PROJECT_DEPLOY_VERSION,
                EI_CLASSIFIER_LABEL_COUNT, EI_CLASSIFIER_RAW_SAMPLE_COUNT);
  Serial.printf("Voice initialization: PSRAM %u bytes, free heap %u bytes.\n",
                ESP.getPsramSize(), ESP.getFreeHeap());
  for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; ++i) {
    if (strcmp(ei_classifier_inferencing_categories[i], "hey_stopmate") == 0) wakeLabel = i;
  }
  if (wakeLabel < 0) {
    Serial.println("Edge Impulse initialization failed; button remains available.");
    return false;
  }
  resetClassifier();
  raw = static_cast<int32_t *>((psramFound() ? ps_malloc(samplesPerFrame * sizeof(int32_t)) : malloc(samplesPerFrame * sizeof(int32_t))));
  pcm = static_cast<int16_t *>((psramFound() ? ps_malloc(samplesPerFrame * sizeof(int16_t)) : malloc(samplesPerFrame * sizeof(int16_t))));
  lock = xSemaphoreCreateMutex();
  bool installed = false;
  bool configured = false;
  if (raw && pcm && lock) {
    i2s_config_t config = {};
    config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
    config.sample_rate = SAMPLE_RATE;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
    config.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    config.dma_buf_count = 16;
    config.dma_buf_len = 512;
    installed = i2s_driver_install(MIC_PORT, &config, 32, &micEvents) == ESP_OK;
    if (installed) {
      i2s_pin_config_t pins = {};
      pins.mck_io_num = I2S_PIN_NO_CHANGE;
      pins.bck_io_num = 27;
      pins.ws_io_num = 32;
      pins.data_out_num = I2S_PIN_NO_CHANGE;
      pins.data_in_num = 33;
      configured = i2s_set_pin(MIC_PORT, &pins) == ESP_OK;
    }
  }
  resumedAt = millis();
  if (configured && xTaskCreatePinnedToCore(detectTask, "wake_detect", 16384,
                                           nullptr, 1, nullptr, 0) == pdPASS) {
    ready = true;
    Serial.printf("Voice ready: say Hey StopMate; %d samples/slice, 16 kHz right channel.\n", samplesPerFrame);
    return true;
  }
  if (installed) i2s_driver_uninstall(MIC_PORT);
  if (lock) vSemaphoreDelete(lock);
  lock = nullptr;
  free(raw);
  free(pcm);
  raw = nullptr;
  pcm = nullptr;
  run_classifier_deinit();
  Serial.println("Microphone/task initialization failed; button remains available.");
  return false;
}

bool takeVoiceTrigger() { return pending.exchange(false); }

bool capturePausedVoiceAudio(int16_t *samples, size_t count) {
  if (!ready || !samples || count <= VOICE_RECORDING_PREROLL_SAMPLES) {
    Serial.println("Recording failed: microphone is not ready.");
    return false;
  }
  // pauseVoiceTrigger() has stopped DMA and locked out the detector. Empty
  // old buffers before restarting; keep clocks running through warmup/countdown.
  bool ok = drainMicrophoneQueue() && i2s_start(MIC_PORT) == ESP_OK;
  if (ok) ok = discardMicrophoneSamples(SAMPLE_RATE / 2);
  for (int second = 3; second > 0 && ok; --second) {
    Serial.printf("%d...\n", second);
    Serial.flush();
    ok = discardMicrophoneSamples(second == 1 ? SAMPLE_RATE - VOICE_RECORDING_PREROLL_SAMPLES : SAMPLE_RATE);
  }
  i2s_event_t event;
  while (xQueueReceive(micEvents, &event, 0) == pdTRUE) {}
  int peak = 0;
  size_t clipped = 0;
  size_t offset = 0;
  auto captureUntil = [&](size_t end) {
    while (offset < end && ok) {
      const size_t frames = min(static_cast<size_t>(256), end - offset);
      size_t bytes = 0;
      ok = i2s_read(MIC_PORT, raw, frames * sizeof(int32_t), &bytes,
                    pdMS_TO_TICKS(1000)) == ESP_OK && bytes == frames * sizeof(int32_t);
      while (xQueueReceive(micEvents, &event, 0) == pdTRUE) {
        if (event.type == I2S_EVENT_RX_Q_OVF || event.type == I2S_EVENT_DMA_ERROR) ok = false;
      }
      if (!ok) break;
      for (size_t i = 0; i < frames; ++i) {
        const int16_t sample = microphoneSample(raw[i]);
        samples[offset + i] = sample;
        peak = max(peak, abs(static_cast<int>(sample)));
        if (sample == -32768 || sample == 32767) ++clipped;
      }
      offset += frames;
    }
  };
  // Preserve the last 300 ms of the countdown for early speech. Continue the
  // SAME stream across the cue: draining here would cut out the first syllable.
  captureUntil(VOICE_RECORDING_PREROLL_SAMPLES);
  if (ok) {
    Serial.printf("Recording now. Speak now. (%u ms + 300 ms pre-roll)\n",
                  static_cast<unsigned>((count - VOICE_RECORDING_PREROLL_SAMPLES) * 1000 / SAMPLE_RATE));
    Serial.flush();
  }
  const uint32_t captureStarted = millis();
  captureUntil(count);
  const uint32_t captureMs = millis() - captureStarted;
  i2s_stop(MIC_PORT);
  if (ok) Serial.printf("Capture complete: %u samples including 300 ms pre-roll; %lu ms after cue; peak=%d/32768, clipped samples=%u/%u.\n",
                        static_cast<unsigned>(count), static_cast<unsigned long>(captureMs),
                        peak, static_cast<unsigned>(clipped), static_cast<unsigned>(count));
  else Serial.println("Recording failed: microphone timeout or dropped samples; clip discarded.");
  return ok;
}

void pauseVoiceTrigger() {
  if (!ready) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  listening = false;
  pending.store(false);
  i2s_stop(MIC_PORT);
}

void resumeVoiceTrigger() {
  if (!ready) return;
  resetClassifier();
  pending.store(false);
  resumedAt = millis();
  listening = true;
  i2s_start(MIC_PORT);
  // The worker drains stale DMA samples during the cooldown without detection.
  xSemaphoreGive(lock);
  Serial.println("Voice listening resumed: Hey StopMate.");
}
