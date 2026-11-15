#include "../TwoButtonPad/AutoFishing.h"
#include "../TwoButtonPad/BiteDetector.h"
#include "../TwoButtonPad/BiteTemplates.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>

void cycle(uint32_t start, uint32_t minimum, uint32_t biteDelay) {
  AutoFishing f;
  f.start(start, minimum);
  assert(f.output() == 2);
  f.update(start+100, false, true, minimum); assert(f.output() == 2);
  f.reportSent(start+200);
  f.update(start+200+minimum-1, false, true, minimum); assert(f.output() == 2);
  f.update(start+200+minimum, false, true, minimum); assert(f.state == AutoFishing::Listening);
  uint32_t listening = start+200+minimum;
  f.update(listening+100, true, true, minimum, biteDelay); assert(f.output() == 0);
  const uint32_t biteAt = listening+1500;
  f.update(biteAt, true, true, minimum, biteDelay);
  assert(f.state == AutoFishing::BiteDelay && f.output() == 0);
  assert(f.reason == AutoFishing::BiteMatched);
  // Changes in proposed random delay and repeated matches must not change the latch.
  f.reportSent(biteAt+10); // a neutral keepalive must not restart the bite wait
  f.update(biteAt+biteDelay-1, true, true, minimum, 1);
  assert(f.state == AutoFishing::BiteDelay && f.output() == 0);
  assert(f.biteWaitMs == biteDelay);
  const uint32_t reelAt = biteAt+biteDelay;
  f.update(reelAt, true, true, minimum, 999);
  assert(f.state == AutoFishing::Reel && f.output() == 1);
  assert(f.reason == AutoFishing::BiteDelayFinished);
  const uint32_t reelSubmittedAt = reelAt+100;
  f.reportSent(reelSubmittedAt);
  f.update(reelSubmittedAt+minimum-1, true, true, minimum); assert(f.output() == 1);
  f.update(reelSubmittedAt+minimum, true, true, minimum);
  assert(f.state == AutoFishing::Rest && f.output() == 0);
  const uint32_t releaseAt = reelSubmittedAt+minimum+2100;
  // Do not recast at all until the release report has actually been submitted.
  f.update(releaseAt-100, true, true, minimum); assert(f.state == AutoFishing::Rest);
  f.reportSent(releaseAt);
  f.update(releaseAt+999, true, true, minimum); assert(f.output() == 0);
  f.update(releaseAt+1000, true, true, minimum); assert(f.output() == 2);
  f.update(releaseAt+1100, true, false, minimum); assert(!f.running());
}
void timeoutAndCancellation() {
  AutoFishing f;
  f.start(0,70); f.reportSent(0); f.update(70,false,true,70);
  for (uint32_t t : {12000u,17000u,19999u,20000u,29999u}) {
    f.update(70+t,false,true,70); assert(f.state == AutoFishing::Listening);
  }
  f.update(30070,false,true,70); assert(f.state == AutoFishing::Cast);
  assert(f.reason == AutoFishing::BiteTimeout);
  for (uint32_t proposed : {0u,999u}) {
    f.start(0,70); f.reportSent(0); f.update(70,false,true,70);
    f.update(1570,true,true,70,proposed);
    assert(f.biteWaitMs == (proposed ? AutoFishing::biteWaitMaxMs : AutoFishing::biteWaitMinMs));
    f.stop(); f.update(5000,true,true,70); assert(f.output() == 0 && !f.running());
    f.start(0,70); f.reportSent(0); f.update(70,false,true,70);
    f.update(1570,true,true,70,proposed);
    f.update(1600,true,false,70); assert(!f.running());
    f.update(9000,true,true,70); assert(!f.running());
  }
}

bool tone(BiteDetector &d, float frequency, unsigned samples) {
  bool result = false;
  for (unsigned i = 0; i < samples; ++i) {
    auto value = int16_t(6000.f * sinf(2*3.14159265f*frequency*i/44100.f));
    result |= d.push(value,value);
  }
  return result;
}
void counters() {
  AutoFishing f;
  f.start(0,150); f.reportSent(0); f.reportSent(100);
  f.update(150,false,true,150);
  f.update(30150,false,true,150); // one actual listening timeout
  assert(f.missed==1 && f.caught==0);
  f.update(30151,false,true,150); assert(f.missed==1);
  f.reportSent(30151); f.update(30301,false,true,150);
  f.update(31801,true,true,150,150);
  assert(f.caught==0); // a sound alone is not a successful submitted Reel
  f.update(31951,false,true,150);
  assert(f.state==AutoFishing::Reel && f.caught==0);
  f.reportSent(31952); f.reportSent(32000);
  assert(f.caught==1 && f.missed==1); // keepalives must not double-count
  f.stop(); f.start(33000,150); f.stop(AutoFishing::CastAudioMissing);
  assert(f.caught==1 && f.missed==1);
  f.start(34000,150); f.update(34001,false,false,150);
  assert(f.caught==1 && f.missed==1); // stop/error/disconnect are not timeouts
  f.caught=f.missed=UINT32_MAX;
  f.start(0,150); f.reportSent(0); f.update(150,false,true,150);
  f.update(30150,false,true,150); assert(f.missed==UINT32_MAX);
  f.reportSent(30150); f.update(30300,false,true,150);
  f.update(31800,true,true,150,150); f.update(31950,false,true,150);
  f.reportSent(31950); assert(f.caught==UINT32_MAX);
}
int main() {
  unsigned cycles=0;
  for (unsigned duration = 150; duration <= 210; ++duration) {
    for (uint32_t wait = AutoFishing::biteWaitMinMs; wait <= AutoFishing::biteWaitMaxMs; ++wait) {
      cycle(0,duration,wait); cycle(UINT32_MAX-1000,duration,wait);
      cycles+=2;
    }
  }
  timeoutAndCancellation();
  counters();
  BiteDetector d; d.setRate(44100);
  assert(!tone(d,900,22050)); // cannot run without a template
  d.loadReferences(biteReferences,biteReferenceCount);
  d.reset(); assert(!tone(d,900,22050));
  d.reset(); assert(!tone(d,2400,22050));
  d.reset(); assert(!tone(d,0,22050));
  printf("%u delay/pulse/rollover cycles, release-anchored pause, timeout, counters, cancellation and spectral checks passed.\n",cycles);
}
