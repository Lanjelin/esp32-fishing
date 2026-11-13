#pragma once
#include <stdint.h>

// Wait briefly before forwarding a single press, so a two-button chord can be
// consumed locally. A quick single tap still produces a short HID press.
class InputRouter {
public:
  static constexpr uint32_t chordWindowMs = 100;
  static constexpr uint32_t tapPulseMs = 20;
  uint8_t output = 0; // bit 0 = Reel, bit 1 = Cast; never both together.

  bool update(uint32_t now, bool reel, bool cast) {
    const uint8_t held = (reel ? 1 : 0) | (cast ? 2 : 0);
    const uint8_t oldHeld = lastHeld;
    lastHeld = held;
    if (state == Chord) {
      output = 0;
      if (!held) state = Idle;
      return false;
    }
    if (held == 3) {
      state = Chord;
      queued = 0;
      output = 0;
      return true;
    }
    if (state == Gap) {
      if (now - since < tapPulseMs) return false;
      state = Pulse;
      output = candidate;
      since = now;
      return false;
    }
    if (state == Pulse) {
      if (held && held != oldHeld) queued = held;
      if (now - since < tapPulseMs) return false;
      output = 0;
      state = Idle;
      if (queued) {
        candidate = queued;
        queued = 0;
        since = now;
        state = held == candidate ? Pending : Gap;
        return false;
      }
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
        since = now;
        output = candidate;
      } else if (now - since >= chordWindowMs) {
        output = candidate;
        state = Active;
      }
    } else if (state == Active && held != candidate) {
      output = 0;
      state = Idle;
      if (held) {
        candidate = held;
        since = now;
        state = Pending;
      }
    }
    return false;
  }

private:
  enum State { Idle, Pending, Active, Pulse, Gap, Chord } state = Idle;
  uint8_t candidate = 0, queued = 0, lastHeld = 0;
  uint32_t since = 0;
};
