#pragma once

#include <stdint.h>

// Converts nominal 100 Hz MAX30102 FIFO samples into the 25 Hz stream
// required by SparkFun's spo2_algorithm.h. Fixed-size storage only; no heap.
class Spo2Stream {
 public:
  enum {
    DECIMATION = 4,
    WINDOW = 100,          // 4 seconds at 25 Hz
    UPDATE_SAMPLES = 25    // one-second hop after the initial window
  };

  Spo2Stream()
      : redSum_(0), irSum_(0), decimationCount_(0), writeIndex_(0),
        samplesStored_(0), samplesSinceUpdate_(0) {}

  // Returns true exactly when a complete 4 s window is available and then
  // once per additional second. The caller supplies one raw paired sample.
  bool push(uint32_t red, uint32_t ir) {
    redSum_ += red;
    irSum_ += ir;
    ++decimationCount_;

    if (decimationCount_ < DECIMATION) return false;

    redRing_[writeIndex_] = redSum_ / DECIMATION;
    irRing_[writeIndex_] = irSum_ / DECIMATION;
    writeIndex_ = (writeIndex_ + 1) % WINDOW;
    redSum_ = 0;
    irSum_ = 0;
    decimationCount_ = 0;

    if (samplesStored_ < WINDOW) {
      ++samplesStored_;
      return samplesStored_ == WINDOW;
    }

    ++samplesSinceUpdate_;
    if (samplesSinceUpdate_ >= UPDATE_SAMPLES) {
      samplesSinceUpdate_ = 0;
      return true;
    }
    return false;
  }

  // Copies the latest 4 s into chronological arrays required by Maxim's API.
  // Call only after push() has returned true.
  void copyChronological(uint32_t* red, uint32_t* ir) const {
    for (uint16_t i = 0; i < WINDOW; ++i) {
      const uint8_t index = (writeIndex_ + i) % WINDOW;
      red[i] = redRing_[index];
      ir[i] = irRing_[index];
    }
  }

  uint8_t samplesStored() const { return samplesStored_; }

 private:
  uint32_t redRing_[WINDOW];
  uint32_t irRing_[WINDOW];
  uint32_t redSum_;
  uint32_t irSum_;
  uint8_t decimationCount_;
  uint8_t writeIndex_;
  uint8_t samplesStored_;
  uint8_t samplesSinceUpdate_;
};
