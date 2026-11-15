#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

// Five ordered ~70ms slices retain the onset/crescendo pattern that a single
// averaged spectrum discards. References are built offline from the three Oggs.
struct BiteSignature {
  static constexpr int slices = 5, bands = 16;
  float spectrum[slices][bands] = {};
  float envelope[slices] = {};
};

class BiteDetector {
public:
  static constexpr int bands = BiteSignature::bands, frameSize = 256;
  static constexpr int slices = BiteSignature::slices, frames = slices * 3;
  static constexpr unsigned analysisRate = 11025;
  BiteSignature reference;
  bool calibrated = false, learning = false, learned = false;
  unsigned sampleRate = 44100, matchedReference = 0;
  uint32_t analysisSamples = 0;
  float analysisSample = 0;
  float lastScore = 0, lastSpectrum = 0, lastEnvelope = 0, lastContrast = 0, lastRms = 0;
  float lastPeakEnergy = 0;
  void loadReferences(const BiteSignature *values, unsigned number) {
    templates = values; templateCount = number;
    calibrated = values && number; learning = learned = false; reset();
  }
  // Host reference extraction only; the device never enters live learning.
  void armLearning() { learning = true; learned = false; reset(); }
  void reset() {
    index = count = head = 0; phase = 0; capturing = false;
    x1 = x2 = y1 = y2 = 0;
    lastScore = lastSpectrum = lastEnvelope = lastContrast = lastRms = lastPeakEnergy = 0;
    memset(history, 0, sizeof(history)); memset(energyHistory, 0, sizeof(energyHistory));
  }
  void setRate(unsigned rate) {
    if (rate == configuredRate) return;
    sampleRate = configuredRate = rate;
    // Anti-alias the downmixed input before producing a fixed 11025Hz stream.
    // Otherwise 48kHz uses a different time base and high-frequency game audio
    // folds into the low-frequency signature bands.
    const float w = 2.f * pi * 3500.f / rate;
    const float c = cosf(w), alpha = sinf(w) / (2.f * .70710678f);
    b0 = (1.f-c) / (2.f*(1.f+alpha)); b1 = 2.f*b0; b2 = b0;
    a1 = -2.f*c/(1.f+alpha); a2 = (1.f-alpha)/(1.f+alpha);
    for (int i = 0; i < bands; ++i)
      coefficient[i] = 2.f*cosf(2.f*pi*180.f*powf(1.2f,i)/analysisRate);
    for (int i = 0; i < frameSize; ++i) window[i] = .5f-.5f*cosf(2.f*pi*i/(frameSize-1));
    reset();
  }
  bool push(int16_t left, int16_t right) {
    const float input = (float(left)+float(right))/65536.f;
    float filtered = b0*input+b1*x1+b2*x2-a1*y1-a2*y2;
    x2=x1; x1=input; y2=y1; y1=filtered;
    phase += analysisRate;
    if (phase < sampleRate) return false;
    phase -= sampleRate;
    analysisSample = filtered; ++analysisSamples;
    block[index++] = filtered;
    if (index != frameSize) return false;
    index = 0;
    float energy = 0;
    for (float x : block) energy += x*x;
    energy /= frameSize;
    lastRms = sqrtf(energy);
    if (learning && !capturing) {
      if (energy < .0004f) return false;
      capturing = true; count = head = 0;
    }
    energyHistory[head] = energy;
    float *slot = history[head];
    for (int band = 0; band < bands; ++band) {
      float a=0, b=0;
      for (int i=0; i<frameSize; ++i) {
        float v=block[i]*window[i]+coefficient[band]*a-b;
        b=a; a=v;
      }
      slot[band] = sqrtf(fmaxf(0,a*a+b*b-coefficient[band]*a*b));
    }
    head = (head+1)%frames;
    if (count < frames) ++count;
    if (count < frames) return false;
    BiteSignature feature;
    float peakEnergy=0;
    if (!makeSignature(feature, peakEnergy)) {
      lastScore=lastSpectrum=lastEnvelope=lastPeakEnergy=0; return false;
    }
    lastPeakEnergy = peakEnergy;
    if (learning) {
      reference=feature; learned=true; learning=capturing=false; return false;
    }
    lastScore=lastSpectrum=lastEnvelope=0;
    if (!calibrated) return false;
    bool matched=false;
    for (unsigned i=0; i<templateCount; ++i) {
      float spectral=0, envelope=0;
      for (int slice=0; slice<slices; ++slice) {
        for (int band=0; band<bands; ++band)
          spectral += templates[i].spectrum[slice][band]*feature.spectrum[slice][band]/slices;
        envelope += templates[i].envelope[slice]*feature.envelope[slice];
      }
      const float score=.65f*spectral+.35f*envelope;
      // Relative contrast rejects steady ambience without a -40dB fixed floor.
      // Tiny hard floor only excludes silence/quantization-scale input.
      const bool passes=spectral>=.96f && envelope>=.95f && lastContrast>=1.6f && peakEnergy>=1.e-8f;
      if ((passes && !matched) || (passes==matched && score>lastScore)) {
        lastScore=score; lastSpectrum=spectral; lastEnvelope=envelope; matchedReference=i;
      }
      matched |= passes;
    }
    return matched;
  }
private:
  static constexpr float pi=3.14159265f;
  const BiteSignature *templates=nullptr;
  unsigned templateCount=0, configuredRate=0, phase=0;
  float coefficient[bands]={}, window[frameSize]={}, block[frameSize]={};
  float history[frames][bands]={}, energyHistory[frames]={};
  float b0=0,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
  int index=0,count=0,head=0;
  bool capturing=false;

  bool makeSignature(BiteSignature &out, float &peak) {
    float energies[slices]={}, floor=1.e30f;
    peak=0; lastContrast=0;
    for (int slice=0; slice<slices; ++slice) {
      for (int frame=0; frame<3; ++frame) {
        const int row=(head+slice*3+frame)%frames; // oldest to newest
        energies[slice] += energyHistory[row]/3.f;
        for (int band=0; band<bands; ++band) out.spectrum[slice][band]+=history[row][band];
      }
      float norm=0;
      for (float v : out.spectrum[slice]) norm+=v*v;
      norm=sqrtf(norm);
      if (norm<1.e-8f) return false;
      for (float &v : out.spectrum[slice]) v/=norm;
      floor=fminf(floor,energies[slice]); peak=fmaxf(peak,energies[slice]);
    }
    lastContrast=peak/fmaxf(floor,1.e-12f);
    float norm=0;
    for (int slice=0; slice<slices; ++slice) {
      out.envelope[slice]=sqrtf(fmaxf(0,energies[slice]-floor));
      norm+=out.envelope[slice]*out.envelope[slice];
    }
    norm=sqrtf(norm);
    if (norm<1.e-8f) return false;
    for (float &v : out.envelope) v/=norm;
    return true;
  }
};
