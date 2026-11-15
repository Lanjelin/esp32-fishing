// Host-only reference builder. Feed little-endian, signed 16-bit stereo PCM.
#include "../TwoButtonPad/BiteDetector.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <cstdlib>
int main(int argc, char **argv) {
  if (argc!=2 && argc!=3) return 2;
  std::ifstream pcm(argv[1],std::ios::binary);
  if (!pcm) return 2;
  BiteDetector detector; detector.setRate(44100); detector.armLearning();
  if (argc==3) {
    const unsigned padding=unsigned(std::stoul(argv[2]));
    if (padding>1023) return 2;
    for (unsigned n=0;n<padding;++n) detector.push(0,0);
  }
  unsigned char frame[4];
  while (pcm.read(reinterpret_cast<char*>(frame),sizeof(frame))) {
    detector.push(int16_t(uint16_t(frame[0])|uint16_t(frame[1])<<8),
                  int16_t(uint16_t(frame[2])|uint16_t(frame[3])<<8));
    if (!detector.learned) continue;
    std::cout<<std::setprecision(9)<<"{\"spectrum\":[";
    for(int s=0;s<BiteSignature::slices;++s) {
      if(s)std::cout<<',';
      std::cout<<'[';
      for(int b=0;b<BiteSignature::bands;++b)std::cout<<(b?",":"")<<detector.reference.spectrum[s][b];
      std::cout<<']';
    }
    std::cout<<"],\"envelope\":[";
    for(int s=0;s<BiteSignature::slices;++s)std::cout<<(s?",":"")<<detector.reference.envelope[s];
    std::cout<<"]}\n";
    return 0;
  }
  std::cerr<<"No suitable audible reference window\n"; return 1;
}
