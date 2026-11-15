#pragma once
#include <Arduino.h>
#include <mbedtls/base64.h>
#include <math.h>

// Bounded, explicitly requested diagnostic export. No flash/filesystem writes,
// microphone input, live calibration or changes to controller state.
class AudioCapture {
public:
  static constexpr uint32_t rate = 11025, durationMs = 40000, baud = 460800;
  bool active = false;
  void start(uint32_t now) {
    if (active) return;
    count = total = 0; started = now;
    Serial.printf("CAPTURE_BEGIN rate=%lu bits=16 baud=%lu duration_ms=%lu\n",
      (unsigned long)rate, (unsigned long)baud, (unsigned long)durationMs);
    Serial.flush();
    Serial.updateBaudRate(baud);
    active = true;
  }
  void push(float value) {
    if (!active) return;
    const int16_t pcm = int16_t(lroundf(fmaxf(-1.f, fminf(32767.f/32768.f,value))*32768.f));
    bytes[count*2] = uint16_t(pcm)&0xff;
    bytes[count*2+1] = uint16_t(pcm)>>8;
    ++count; ++total;
    if (count == packetSamples) flushPacket();
  }
  void poll(uint32_t now) {
    if (!active || now-started < durationMs) return;
    flushPacket();
    Serial.printf("CAPTURE_END samples=%lu\n",(unsigned long)total);
    Serial.flush();
    active=false;
    Serial.updateBaudRate(115200);
  }
private:
  static constexpr unsigned packetSamples = 256;
  uint8_t bytes[packetSamples*2] = {};
  unsigned char encoded[686] = {};
  uint32_t started=0, total=0;
  unsigned count=0;
  void flushPacket() {
    if (!count) return;
    size_t length=0;
    if (mbedtls_base64_encode(encoded,sizeof(encoded),&length,bytes,count*2)==0) {
      encoded[length]=0;
      Serial.printf("CAPTURE_ROW %lu %s\n",(unsigned long)(total-count),encoded);
    } else Serial.println("CAPTURE_ERROR encode");
    count=0;
  }
};
