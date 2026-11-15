// Replay transformed PCM through the actual firmware detector, not a Python clone.
#include "../TwoButtonPad/BiteDetector.h"
#include "../TwoButtonPad/BiteTemplates.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
int main(int argc, char **argv) {
  if (argc != 4) return 2;
  std::ifstream pcm(argv[1], std::ios::binary);
  if (!pcm) return 2;
  BiteDetector detector;
  detector.setRate(unsigned(std::stoul(argv[2])));
  detector.loadReferences(biteReferences, biteReferenceCount);
  unsigned char frame[4];
  bool matched = false;
  float best = 0;
  while (pcm.read(reinterpret_cast<char*>(frame), sizeof(frame))) {
    auto left = int16_t(uint16_t(frame[0]) | uint16_t(frame[1]) << 8);
    auto right = int16_t(uint16_t(frame[2]) | uint16_t(frame[3]) << 8);
    matched |= detector.push(left, right);
    if (detector.lastScore > best) best = detector.lastScore;
  }
  const bool expected = std::string(argv[3]) == "match";
  std::cout << "matched=" << matched << " best_similarity=" << best << '\n';
  return matched == expected ? 0 : 1;
}
