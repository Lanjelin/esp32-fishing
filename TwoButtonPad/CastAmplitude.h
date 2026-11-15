#pragma once
#include <math.h>
#include <stdint.h>

// RAM-only calibration from the first submitted cast of each Start/Stop run.
// Input is the detector's post-filter 11025 Hz stream. Measure the same 70 ms
// slice energy used by bite signatures, independently of detector resets.
class CastAmplitude {
public:
  static constexpr uint32_t windowMs = 1500;
  static constexpr float minimumRatio = 1.f;
  enum State { Off, Collecting, Ready, Invalid } state = Off;
  float baselineEnergy = 0;
  void reset() { *this = CastAmplitude(); }
  void begin(uint32_t now) { reset(); state = Collecting; started = now; }
  void push(float sample) {
    if (state != Collecting) return;
    ++samples;
    sum += sample * sample;
    if (++inBlock != 256) return;
    energies[head] = sum / 256.f;
    head = (head + 1) % 3;
    if (blocks < 3) ++blocks;
    if (blocks == 3) {
      const float energy = (energies[0] + energies[1] + energies[2]) / 3.f;
      if (energy > baselineEnergy) baselineEnergy = energy;
    }
    sum = 0; inBlock = 0;
  }
  bool poll(uint32_t now) {
    if (state != Collecting || uint32_t(now - started) < windowMs) return false;
    // At least a second of actual audio; silence/quantization is not a baseline.
    state = samples >= 11025 && isfinite(baselineEnergy) && baselineEnergy >= 1.e-8f
      ? Ready : Invalid;
    return true;
  }
  bool accepts(float peakEnergy) const {
    return state == Ready && isfinite(peakEnergy) &&
      peakEnergy >= baselineEnergy * minimumRatio * minimumRatio;
  }
private:
  uint32_t started = 0, samples = 0;
  unsigned inBlock = 0, head = 0, blocks = 0;
  float sum = 0, energies[3] = {};
};
