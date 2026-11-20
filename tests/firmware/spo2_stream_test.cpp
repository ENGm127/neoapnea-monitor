#include <assert.h>
#include <stdint.h>
#include "spo2_stream.h"

int main() {
  Spo2Stream stream;
  uint32_t red[Spo2Stream::WINDOW];
  uint32_t ir[Spo2Stream::WINDOW];

  // No output before 100 decimated samples (400 raw samples).
  for (uint32_t group = 1; group <= 99; ++group) {
    for (uint8_t i = 0; i < Spo2Stream::DECIMATION; ++i) {
      assert(!stream.push(group, 1000 + group));
    }
  }
  for (uint8_t i = 0; i < Spo2Stream::DECIMATION - 1; ++i) {
    assert(!stream.push(100, 1100));
  }
  assert(stream.push(100, 1100));
  assert(stream.samplesStored() == Spo2Stream::WINDOW);

  stream.copyChronological(red, ir);
  for (uint32_t i = 0; i < Spo2Stream::WINDOW; ++i) {
    assert(red[i] == i + 1);
    assert(ir[i] == 1000 + i + 1);
  }

  // Sliding window updates only after 25 new decimated samples (one second).
  for (uint8_t group = 0; group < Spo2Stream::UPDATE_SAMPLES - 1; ++group) {
    for (uint8_t i = 0; i < Spo2Stream::DECIMATION; ++i) {
      assert(!stream.push(101 + group, 1101 + group));
    }
  }
  for (uint8_t i = 0; i < Spo2Stream::DECIMATION - 1; ++i) {
    assert(!stream.push(125, 1125));
  }
  assert(stream.push(125, 1125));

  // Wrap-around must still copy the newest 100 samples in chronological order.
  for (uint32_t group = 126; group <= 225; ++group) {
    for (uint8_t i = 0; i < Spo2Stream::DECIMATION; ++i) {
      const bool update = stream.push(group, 1000 + group);
      if ((group - 126) % Spo2Stream::UPDATE_SAMPLES == Spo2Stream::UPDATE_SAMPLES - 1 &&
          i == Spo2Stream::DECIMATION - 1) {
        assert(update);
      }
    }
  }
  stream.copyChronological(red, ir);
  for (uint32_t i = 0; i < Spo2Stream::WINDOW; ++i) {
    assert(red[i] == 126 + i);
    assert(ir[i] == 1126 + i);
  }
  return 0;
}
