#pragma once

#include <stdint.h>
#include <string.h>

const uint8_t NEO_GRB = 0;
const uint8_t NEO_KHZ800 = 0;

// Capture frames sent by the actual sketch without driving physical LEDs.
class Adafruit_NeoPixel {
 public:
  Adafruit_NeoPixel(uint16_t count, int, uint8_t) : count_(count) {
    clear();
    memset(shown_, 0, sizeof(shown_));
  }

  void begin() { begun = true; }
  void clear() { memset(buffer_, 0, sizeof(buffer_)); }
  void setPixelColor(uint16_t index, uint32_t color) {
    if (index >= count_ || index >= 108) {
      ++invalidWrites;
      return;
    }
    buffer_[index] = color;
  }
  void show() {
    memcpy(shown_, buffer_, sizeof(shown_));
    ++showCount;
  }
  uint32_t shownColor(uint16_t index) const { return shown_[index]; }
  static uint32_t Color(uint8_t r, uint8_t g, uint8_t b) {
    return (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
  }

  bool begun = false;
  unsigned int showCount = 0;
  unsigned int invalidWrites = 0;

 private:
  uint16_t count_;
  uint32_t buffer_[108];
  uint32_t shown_[108];
};
