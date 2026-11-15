// Broadband-noise stress regression for the strict matcher.
#include "../TwoButtonPad/BiteTemplates.h"
#include <cmath>
#include <cstdio>
#include <cstdint>
int main(){
 unsigned passing=0, matches=0;
 for(unsigned kind=0;kind<4;++kind)for(unsigned seed=1;seed<=128;++seed){
  BiteDetector d;d.setRate(44100);d.loadReferences(biteReferences,biteReferenceCount);
  uint32_t rng=seed;bool matched=false;
  for(unsigned i=0;i<44100*5;++i){
   rng=rng*1664525u+1013904223u;
   const float t=float(i%44100)/44100;
   float gain=kind==0?t:kind==1?expf(-t*6):kind==2?1.f:(t<.3f?t/.3f:expf(-(t-.3f)*10));
   int16_t v=int16_t((int(rng>>16)-32768)*.2f*gain);
   if(d.push(v,v) && d.lastPeakEnergy>=.005f*.005f){matched=true;++matches;}
  }
  if(matched){++passing;printf("MATCH kind=%u seed=%u\n",kind,seed);}
 }
 printf("Noise challenge: %u/512 clips matched, %u passing frames\n",passing,matches);
 return passing?1:0;
}
