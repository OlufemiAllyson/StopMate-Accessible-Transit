#pragma once
#include <cstdint>

// Pure detection logic; a wave is an approach after a clear sensing area.
class WaveDetector {
 public:
  void reset(uint32_t now, uint32_t cooldown = 3000) {
    armed_ = false;
    clearing_ = false;
    nearCount_ = 0;
    resetAt_ = now;
    cooldown_ = cooldown;
  }

  // distanceCm == 0 means no echo (clear/out of range).
  bool sample(uint32_t now, uint32_t distanceCm) {
    if (now - resetAt_ < cooldown_) return false;
    const bool near = distanceCm >= 2 && distanceCm <= 30;
    const bool clear = distanceCm == 0 || distanceCm >= 40;
    if (!armed_) {
      if (!clear) {
        clearing_ = false;
      } else if (!clearing_) {
        clearing_ = true;
        clearSince_ = now;
      } else if (now - clearSince_ >= 300) {
        armed_ = true;
        nearCount_ = 0;
      }
      return false;
    }
    if (!near) {
      nearCount_ = 0;
      return false;
    }
    if (nearCount_ == 0 || now - lastNear_ > 250) {
      nearCount_ = 1;
      lastNear_ = now;
      return false;
    }
    reset(now);
    return true;
  }

 private:
  bool armed_ = false;
  bool clearing_ = false;
  uint8_t nearCount_ = 0;
  uint32_t clearSince_ = 0;
  uint32_t lastNear_ = 0;
  uint32_t resetAt_ = 0;
  uint32_t cooldown_ = 0;
};
