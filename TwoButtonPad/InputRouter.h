#pragma once
#include <stdint.h>

// Arbitrate the UI chord before forwarding singles. Each forwarded press gets
// one minimum duration; count it from the first submitted HID report.
class InputRouter {
public:
  static constexpr uint32_t chordWindowMs = 100;
  static constexpr uint32_t minimumPressMinMs = 60;
  static constexpr uint32_t minimumPressMaxMs = 80;
  static constexpr uint32_t releaseGapMs = 20;
  uint8_t output = 0; // bit 0 = Reel, bit 1 = Cast; never both together.

  void noteReportSent(uint32_t now) {
    if (output && !reported) { reported = true; outputSince = now; }
  }
  void restartReportTiming() { reported = false; }

  bool update(uint32_t now, bool reel, bool cast, uint32_t requestedMinimumMs = 70) {
    const uint8_t held = (reel ? 1 : 0) | (cast ? 2 : 0);
    const uint8_t oldHeld = lastHeld;
    lastHeld = held;
    if (state == Chord) {
      if (output && minimumElapsed(now)) output = 0;
      if (!held && !output) state = Idle;
      return false;
    }
    if (held == 3) {
      state = Chord;
      queued = 0;
      // An already-submitted single cannot be undone. Finish its minimum,
      // without ever forwarding the second button of this late overlap.
      if (output && minimumElapsed(now)) output = 0;
      return true;
    }
    if (state == Gap) {
      if (now - since < releaseGapMs) return false;
      state = Pulse;
      startPress(candidate, requestedMinimumMs);
      return false;
    }
    if (state == Pulse) {
      if (held && held != oldHeld) queued = held;
      if (!minimumElapsed(now)) return false;
      finishPress(now, held);
      return false;
    }
    if (state == Idle && held) {
      state = Pending;
      candidate = held;
      since = now;
    }
    if (state == Pending) {
      if (held != candidate) {
        queued = held;
        state = Pulse;
        startPress(candidate, requestedMinimumMs);
      } else if (now - since >= chordWindowMs) {
        startPress(candidate, requestedMinimumMs);
        state = Active;
      }
    } else if (state == Active && held != candidate) {
      queued = held;
      state = Pulse;
      if (minimumElapsed(now)) finishPress(now, held);
    }
    return false;
  }

private:
  enum State { Idle, Pending, Active, Pulse, Gap, Chord } state = Idle;
  uint8_t candidate = 0, queued = 0, lastHeld = 0;
  uint32_t since = 0, outputSince = 0, minimumMs = 70;
  bool reported = false;

  bool minimumElapsed(uint32_t now) const {
    return reported && now - outputSince >= minimumMs;
  }
  void startPress(uint8_t button, uint32_t duration) {
    output = button;
    reported = false;
    minimumMs = duration < minimumPressMinMs ? minimumPressMinMs
              : duration > minimumPressMaxMs ? minimumPressMaxMs : duration;
  }
  void finishPress(uint32_t now, uint8_t held) {
    output = 0;
    reported = false;
    state = Idle;
    if (queued) {
      candidate = queued;
      queued = 0;
      since = now;
      state = held == candidate ? Pending : Gap;
    }
  }
};
