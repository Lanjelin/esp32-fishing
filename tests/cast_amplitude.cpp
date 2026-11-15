#include "../TwoButtonPad/CastAmplitude.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

void fill(CastAmplitude &gate, float amplitude, unsigned count = 16537) {
  for (unsigned i=0;i<count;++i) gate.push(amplitude*sinf(i*.31f));
}
int main() {
  for (float level : {0.0002f,0.005f,0.1f}) {
    CastAmplitude gate;
    assert(!gate.accepts(1));
    gate.begin(123); fill(gate,level);
    assert(!gate.poll(1622)); assert(!gate.accepts(1));
    assert(gate.poll(1623)); assert(gate.state==CastAmplitude::Ready);
    const float reference=gate.baselineEnergy;
    assert(gate.accepts(reference)); assert(gate.accepts(reference*16));
    assert(!gate.accepts(reference*.01f));
    assert(!gate.accepts(std::numeric_limits<float>::quiet_NaN()));
    assert(!gate.accepts(std::numeric_limits<float>::infinity()));
    fill(gate,1); assert(!gate.poll(9999)); assert(gate.baselineEnergy==reference);
    gate.reset(); assert(gate.state==CastAmplitude::Off && !gate.accepts(1));
    gate.begin(9999); fill(gate,level*.75f); gate.poll(11499);
    assert(gate.state==CastAmplitude::Ready && gate.baselineEnergy<reference*.6f);
  }
  CastAmplitude gate;
  gate.begin(UINT32_MAX-500); fill(gate,.005f);
  assert(!gate.poll(998)); assert(gate.poll(999)); assert(gate.state==CastAmplitude::Ready);
  gate.begin(0); fill(gate,0); assert(gate.poll(1500));
  assert(gate.state==CastAmplitude::Invalid && !gate.accepts(1));
  gate.begin(0); fill(gate,.005f,1000); gate.poll(1500);
  assert(gate.state==CastAmplitude::Invalid && !gate.accepts(1));
  gate.begin(0); fill(gate,1.e-6f); gate.poll(1500);
  assert(gate.state==CastAmplitude::Invalid);
  puts("Cast amplitude scaling, frozen baseline, reset, missing/silent audio, nonfinite input and clock rollover passed.");
}
