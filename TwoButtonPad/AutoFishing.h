#pragma once
#include <stdint.h>

// No delays: physical stop/exit and disconnect are handled on every loop.
class AutoFishing {
public:
  enum State { Stopped, Cast, Listening, BiteDelay, Reel, Rest } state = Stopped;
  enum Reason { Initial, UserStart, UserStop, ModeExit, AudioOverrun, NotReady,
    CastReleased, BiteMatched, BiteDelayFinished, BiteTimeout, ReelReleased, ReelDelayFinished,
    CastAudioMissing } reason = Initial;
  static constexpr uint32_t noBiteTimeoutMs = 30000;
  static constexpr uint32_t landingIgnoreMs = 1500;
  static constexpr uint32_t reelPauseMs = 1000;
  static constexpr uint32_t biteWaitMinMs = 150, biteWaitMaxMs = 300;
  uint32_t biteWaitMs = (biteWaitMinMs + biteWaitMaxMs) / 2;
  uint32_t since = 0, minimum = 180;
  // Session totals survive Stop/Start, but not reboot. Caught means a queued
  // bite-triggered Reel, not independently verified fish/loot.
  uint32_t caught = 0, missed = 0;
  bool submitted = false;
  uint8_t output() const { return state == Cast ? 2 : state == Reel ? 1 : 0; }
  bool running() const { return state != Stopped; }
  void stop(Reason why = UserStop) {
    if (running()) reason = why;
    state = Stopped; submitted = false;
  }
  void start(uint32_t now, uint32_t duration) { enter(Cast, now, duration, UserStart); }
  void reportSent(uint32_t now) {
    // Rest starts counting only once the neutral/release report is submitted.
    if ((output() || state == Rest) && !submitted) {
      if (state == Reel && caught < UINT32_MAX) ++caught;
      submitted = true; since = now;
    }
  }
  void update(uint32_t now, bool bite, bool ready, uint32_t duration,
      uint32_t requestedBiteWaitMs = (biteWaitMinMs + biteWaitMaxMs) / 2) {
    if (!ready) { stop(NotReady); return; }
    if (state == Cast && submitted && now - since >= minimum) enter(Listening, now, duration, CastReleased);
    else if (state == Listening) {
      if (bite && now - since >= landingIgnoreMs) {
        // Latch once per accepted bite; new random values on later loops must
        // not move this deadline. Repeated sound matches cannot restart it.
        biteWaitMs = requestedBiteWaitMs < biteWaitMinMs ? biteWaitMinMs
          : requestedBiteWaitMs > biteWaitMaxMs ? biteWaitMaxMs : requestedBiteWaitMs;
        enter(BiteDelay, now, duration, BiteMatched);
      } else if (now - since >= noBiteTimeoutMs) {
        if (missed < UINT32_MAX) ++missed;
        enter(Cast, now, duration, BiteTimeout);
      }
    } else if (state == BiteDelay && now - since >= biteWaitMs) enter(Reel, now, duration, BiteDelayFinished);
    else if (state == Reel && submitted && now - since >= minimum) enter(Rest, now, duration, ReelReleased);
    else if (state == Rest && submitted && now - since >= reelPauseMs) enter(Cast, now, duration, ReelDelayFinished);
  }
  static const char *stateName(State value) {
    switch (value) {
      case Stopped: return "stopped";
      case Cast: return "cast";
      case Listening: return "listening";
      case BiteDelay: return "bite-wait";
      case Reel: return "reel";
      case Rest: return "rest";
    }
    return "unknown";
  }
  static const char *reasonName(Reason value) {
    switch (value) {
      case Initial: return "initial";
      case UserStart: return "user-start";
      case UserStop: return "user-stop";
      case ModeExit: return "mode-exit";
      case AudioOverrun: return "audio-overrun";
      case CastAudioMissing: return "first-cast-audio-missing";
      case NotReady: return "connection-or-audio-unavailable";
      case CastReleased: return "cast-released";
      case BiteMatched: return "bite-match";
      case BiteDelayFinished: return "150-300ms-after-bite";
      case BiteTimeout: return "no-bite-30s-timeout";
      case ReelReleased: return "reel-released";
      case ReelDelayFinished: return "1s-after-reel";
    }
    return "unknown";
  }
private:
  void enter(State next, uint32_t now, uint32_t duration, Reason why) {
    reason = why;
    state = next; since = now; minimum = duration; submitted = false;
  }
};
