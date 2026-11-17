// Original LilyGO T-Display v1.1 (ESP32, not S3).
#include <Arduino.h>
#include <Preferences.h>
#include <esp_random.h>
#include "DualBluetooth.h"
#include "AutoFishing.h"
#include "BiteDetector.h"
#include "CastAmplitude.h"
#include "BiteTemplates.h"
#include "AudioCapture.h"
#include <freertos/stream_buffer.h>
#include <M5GFX.h>
#include <mbedtls/base64.h>
#include "FishingIcon.h"
#include "InputRouter.h"
#include <lgfx/v1/panel/Panel_ST7789.hpp>
#include <lgfx/v1/platforms/esp32/Bus_SPI.hpp>

FishingPad pad;
AutoFishing automatic;
BiteDetector detector;
CastAmplitude castAmplitude;
AudioCapture audioCapture;
StreamBufferHandle_t audioBuffer;
std::atomic<bool> audioOverflow{false};
std::atomic<uint32_t> lastAudio{0};
bool autonomousMode = false, gestureConsumed = false, waitingRelease = false;
uint32_t castHeldSince = 0;
bool pendingCast = false, pendingReel = false, diagnostic = false;
uint32_t reelHeldSince = 0;
void audioSamples(const uint8_t *data, uint32_t length) {
  lastAudio = millis();
  // Drop whole PCM packets on overload; never lose stereo frame alignment.
  if (length % 4 || xStreamBufferSpacesAvailable(audioBuffer) < length) audioOverflow = true;
  else if (xStreamBufferSend(audioBuffer, data, length, 0) != length) audioOverflow = true;
}

class TDisplay : public lgfx::LGFX_Device {
  lgfx::Bus_SPI bus;
  lgfx::Panel_ST7789 panel;
public:
  TDisplay() {
    auto b = bus.config();
    b.spi_host = VSPI_HOST;
    b.spi_mode = 0;
    b.freq_write = 20000000;
    b.freq_read = 10000000;
    b.spi_3wire = true;
    b.use_lock = true;
    b.dma_channel = 0;
    b.pin_sclk = 18; b.pin_mosi = 19; b.pin_miso = -1; b.pin_dc = 16;
    bus.config(b);
    panel.setBus(&bus);
    auto p = panel.config();
    p.pin_cs = 5; p.pin_rst = 23; p.pin_busy = -1;
    p.memory_width = 240; p.memory_height = 320;
    p.panel_width = 135; p.panel_height = 240;
    p.offset_x = 52; p.offset_y = 40;
    p.offset_rotation = 0;
    p.invert = true; p.rgb_order = false; p.readable = false;
    panel.config(p);
    setPanel(&panel);
  }
};
TDisplay screen;
lgfx::LGFXBase *canvas = &screen;
struct Button {
  uint8_t pin;
  bool raw = false, down = false;
  uint32_t changed = 0;
  bool poll(uint32_t now) {
    bool value = digitalRead(pin) == LOW;
    if (value != raw) { raw = value; changed = now; }
    if (raw != down && now - changed >= 15) { down = raw; return true; }
    return false;
  }
};
Button gpio0Button{0}, gpio35Button{35};
bool previousConnection = false;
uint32_t lastActivity = 0, lastReport = 0;
Preferences uiPreferences;
bool neutralMode = false, displayFlipped = false;
Button &reelButton();
Button &castButton();
Button &reelButton() { return displayFlipped ? gpio35Button : gpio0Button; }
Button &castButton() { return displayFlipped ? gpio0Button : gpio35Button; }
InputRouter inputRouter;
uint8_t previousHid = 0;
uint32_t displayedSecond = UINT32_MAX;
uint32_t timerStartedAt = 0;
uint32_t timerCaught = UINT32_MAX, timerMissed = UINT32_MAX;

void formatCounts(char *buffer, size_t size, const char *label) {
  char caught[8], missed[8];
  snprintf(caught,sizeof(caught),"%lu%s",(unsigned long)(automatic.caught>9999 ? 9999 : automatic.caught),automatic.caught>9999 ? "+" : "");
  snprintf(missed,sizeof(missed),"%lu%s",(unsigned long)(automatic.missed>9999 ? 9999 : automatic.missed),automatic.missed>9999 ? "+" : "");
  snprintf(buffer,size,"%s %s/%s",label,caught,missed);
}

void drawTimer(bool connected, bool fullRefresh = true) {
  const uint16_t foreground = canvas->color565(180, 187, 191);
  if (fullRefresh) {
    canvas->fillScreen(TFT_BLACK);
    canvas->setTextColor(foreground, TFT_BLACK);
    canvas->drawCentreString("DESK TIMER", 120, 12, 2);
    canvas->setTextColor(canvas->color565(91, 100, 105), TFT_BLACK);
    canvas->drawCentreString(connected ? "Bluetooth connected" : "Bluetooth ready", 120, 111, 2);
  }
  uint32_t elapsed = uint32_t(millis() - timerStartedAt) / 1000;
  char timer[12];
  snprintf(timer, sizeof(timer), "%02lu:%02lu:%02lu",
    (unsigned long)((elapsed / 3600) % 100),
    (unsigned long)((elapsed / 60) % 60), (unsigned long)(elapsed % 60));
  canvas->setTextColor(foreground, TFT_BLACK);
  canvas->setTextPadding(220);
  canvas->drawCentreString(timer, 120, 45, 6);
  canvas->setTextPadding(0);
  if (autonomousMode && (fullRefresh || timerCaught != automatic.caught || timerMissed != automatic.missed)) {
    char counts[24];
    formatCounts(counts, sizeof(counts), "Count");
    canvas->fillRect(0, 89, 240, 20, TFT_BLACK);
    canvas->setTextColor(foreground, TFT_BLACK);
    canvas->drawCentreString(counts, 120, 90, 2);
    if (canvas == &screen) { timerCaught = automatic.caught; timerMissed = automatic.missed; }
  }
}


void tile(bool cast, const char *label, bool pressed, uint16_t accent) {
  const int x = displayFlipped ? 8 : 124;
  const int y = cast ? 49 : 93;
  const uint16_t background = pressed ? accent : canvas->color565(20, 29, 36);
  canvas->fillRoundRect(x, y, 108, 38, 7, background);
  canvas->drawRoundRect(x, y, 108, 38, 7, accent);
  canvas->setTextColor(pressed ? TFT_BLACK : TFT_WHITE, background);
  canvas->drawCentreString(label, x + 54, y + 7, 4);
}
const char *autoStatus() {
  if (!FishingAudio::streaming || millis() - lastAudio.load() >= 2000) return "AUTO: select Desk Audio";
  if (!detector.calibrated) return "AUTO: references missing";
  if (!automatic.running() && automatic.reason == AutoFishing::CastAudioMissing) return "AUTO: cast audio missing";
  return automatic.running() ? "AUTO: running" : "AUTO: stopped";
}
const char *lastAutoStatus = nullptr;
uint32_t displayedCaught = UINT32_MAX, displayedMissed = UINT32_MAX;
void drawAutoStatus() {
  const char *status = autoStatus();
  canvas->fillRect(0, 22, 240, 20, TFT_BLACK);
  canvas->setTextColor(automatic.running() ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
  canvas->drawString(status, 8, 24, 2);
  // Bound display width even during very long sessions; totals remain 32-bit.
  char counts[24];
  formatCounts(counts, sizeof(counts), "Fish");
  // Prioritize explanatory fault text over counters if that line is too long.
  if (canvas->textWidth(status,&lgfx::fonts::Font2)+8+canvas->textWidth(counts,&lgfx::fonts::Font2)<=224) {
    canvas->setTextColor(TFT_WHITE,TFT_BLACK);
    canvas->drawRightString(counts,232,24,2);
  }
  if (canvas == &screen) {
    lastAutoStatus = status;
    displayedCaught = automatic.caught; displayedMissed = automatic.missed;
  }
}
void draw(bool connected) {
  if (neutralMode) { drawTimer(connected); return; }
  canvas->fillScreen(TFT_BLACK);
  canvas->setTextColor(canvas->color565(239, 201, 112), TFT_BLACK);
  canvas->drawCentreString("Two Button Fishing Pad", 120, 3, 2);
  canvas->setTextColor(connected ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
  canvas->drawString(pad.statusText(), 8, 24, 2);
  canvas->drawFastHLine(8, 43, 224, canvas->color565(69, 56, 32));
  // Keep controls beside their physical buttons; the icon uses the other side.
  const int iconX = displayFlipped ? 144 : 16;
  // Original WoW Fishing profession icon, framed in warm gold.
  canvas->fillRoundRect(iconX, 49, 80, 80, 8, canvas->color565(25, 21, 15));
  canvas->drawRoundRect(iconX, 49, 80, 80, 8, canvas->color565(189, 148, 67));
  canvas->drawRoundRect(iconX + 3, 52, 74, 74, 6, canvas->color565(91, 66, 32));
  canvas->drawJpg(fishingIconJpg, sizeof(fishingIconJpg), iconX + 12, 61);
  tile(true, autonomousMode ? "Exit(2s)" : "Cast", castButton().down, TFT_GREEN);
  tile(false, autonomousMode ? (automatic.running() ? "Stop" : "Start") : "Reel", reelButton().down, TFT_CYAN);
  if (autonomousMode) drawAutoStatus();
}
// Render the same UI into an offscreen buffer for an on-device screenshot.
// Send 's' over serial; rows are RGB888/base64, exported by tools/capture_ui.py.
void screenshot(bool deskTimerPreview = false) {
  lgfx::LGFX_Sprite shot(&screen);
  shot.setColorDepth(16);
  if (!shot.createSprite(240, 135)) { Serial.println("SHOT_ERROR memory"); return; }
  canvas = &shot;
  if (deskTimerPreview) drawTimer(pad.isConnected());
  else draw(pad.isConnected());
  canvas = &screen;
  Serial.println("SHOT_BEGIN 240 135");
  uint8_t rgb[240 * 3];
  unsigned char encoded[961];
  for (int y = 0; y < 135; ++y) {
    for (int x = 0; x < 240; ++x) {
      uint16_t pixel = shot.readPixel(x, y); // M5GFX returns RGB565.
      rgb[x * 3] = ((pixel >> 11) & 31) * 255 / 31;
      rgb[x * 3 + 1] = ((pixel >> 5) & 63) * 255 / 63;
      rgb[x * 3 + 2] = (pixel & 31) * 255 / 31;
    }
    size_t length = 0;
    mbedtls_base64_encode(encoded, sizeof(encoded), &length, rgb, sizeof(rgb));
    encoded[length] = 0;
    Serial.printf("SHOT_ROW %d %s\n", y, encoded);
  }
  Serial.println("SHOT_END");
}
void setup() {
  Serial.begin(115200);
  pinMode(0, INPUT_PULLUP);
  // GPIO35 has no internal pull-up; original T-Display provides it on-board.
  pinMode(35, INPUT);
  pinMode(4, OUTPUT);
  screen.init();
  // Restore orientation before the first draw.
  uiPreferences.begin("fishing-ui", false);
  displayFlipped = uiPreferences.getBool("flipped", false);
  screen.setRotation(displayFlipped ? 3 : 1);
  // Full brightness; keep the validated backlight setup.
  pinMode(4, OUTPUT);
  digitalWrite(4, HIGH);
  Serial.println("Display: M5GFX ST7789 135x240, mode0 20MHz, GPIO18/19/5/16/23 BL4");
  neutralMode = uiPreferences.getBool("neutral", false);
  draw(false);
  audioBuffer = xStreamBufferCreate(32768, 4);
  if (!audioBuffer || !pad.begin()) {
    Serial.println("Bluetooth/audio initialization FAILED");
    while (true) delay(1000);
  }
  FishingAudio::pcmConsumer = audioSamples;
  detector.setRate(44100);
  // Preloaded, computer-generated references always take precedence over any
  // legacy live-calibration blob. No user calibration or NVS upload is needed.
  detector.loadReferences(biteReferences, biteReferenceCount);
  Serial.println("Detector: ordered spectral/envelope signatures; fixed 11025Hz analysis; relative contrast gate.");
  Serial.printf("Bite references ready: %u timing/pitch templates for %u sounds. Cast loudness calibrates on Start.\n", biteReferenceCount, biteSourceCount);
  Serial.printf("Fishing pad ready: GPIO%u=Cast (Left), GPIO%u=Reel (X).\n", unsigned(castButton().pin), unsigned(reelButton().pin));
  lastActivity = millis();
}
void loop() {
  if (Serial.available()) {
    char command = Serial.read();
    if (command == 's') screenshot();
    else if (command == 't') screenshot(true);
    else if (command == 'b') pad.reportBondStatus();
    else if (command == 'a') {
      if (!audioCapture.active) {
        if (autonomousMode && FishingAudio::streaming && millis()-lastAudio.load()<2000)
          audioCapture.start(millis());
        else Serial.println("CAPTURE_NOT_READY enable audio mode and select Desk Audio");
      }
    }
    else if (command == 'v') {
      diagnostic = !diagnostic;
      Serial.printf("Bite diagnostics: %s\n", diagnostic ? "on" : "off");
    } else if (command == 'c') {
      Serial.println("Live sound learning disabled: the reference bank is preloaded.");
    }
  }
  uint32_t now = millis();
  audioCapture.poll(now);
  const bool changed0 = gpio0Button.poll(now);
  const bool changed35 = gpio35Button.poll(now);
  const bool changedReel = displayFlipped ? changed35 : changed0;
  const bool changedCast = displayFlipped ? changed0 : changed35;
  pad.poll(now);
  bool connected = pad.isConnected();
  static const char *previousBleStatus = nullptr;
  const char *bleStatus = pad.statusText();
  bool changedBleStatus = bleStatus != previousBleStatus;
  previousBleStatus = bleStatus;
  bool changedConnection = connected != previousConnection;
  bool audioReady = FishingAudio::streaming && now - lastAudio.load() < 2000;
  bool bite = false;
  float matchedScore=0, matchedSpectrum=0, matchedShape=0, matchedContrast=0;
  unsigned matchedId=0;
  uint32_t matchedSample=0;
  const size_t queuedBytes = xStreamBufferBytesAvailable(audioBuffer);
  detector.setRate(FishingAudio::rate);
  uint8_t pcm[4096];
  size_t received = xStreamBufferReceive(audioBuffer, pcm, sizeof(pcm), 0);
  if (audioOverflow.exchange(false)) {
    automatic.stop(AutoFishing::AudioOverrun); detector.reset();
    Serial.println("Audio overflow: stopped; reduce serial/UI activity.");
  }
  for (size_t i = 0; i + 3 < received; i += 4) {
    int16_t l = int16_t(uint16_t(pcm[i]) | uint16_t(pcm[i+1]) << 8);
    int16_t r = int16_t(uint16_t(pcm[i+2]) | uint16_t(pcm[i+3]) << 8);
    const uint32_t previousSamples=detector.analysisSamples;
    const bool sampleMatched = detector.push(l, r);
    if (sampleMatched && !bite) {
      matchedScore=detector.lastScore; matchedSpectrum=detector.lastSpectrum;
      matchedShape=detector.lastEnvelope; matchedContrast=detector.lastContrast;
      matchedId=detector.matchedReference; matchedSample=detector.analysisSamples;
    }
    if (detector.analysisSamples!=previousSamples) castAmplitude.push(detector.analysisSample);
    const bool amplitudePass = !automatic.running() || castAmplitude.accepts(detector.lastPeakEnergy);
    bite |= sampleMatched && amplitudePass;
    static uint32_t lastQuietMatch = 0;
    if (sampleMatched && !amplitudePass && automatic.state == AutoFishing::Listening && now-lastQuietMatch>500) {
      Serial.printf("QUIET_BITE t=%lu file=%lu peak_rms=%.6f cast_rms=%.6f\n",
        (unsigned long)now,(unsigned long)biteReferenceIds[detector.matchedReference],
        sqrtf(detector.lastPeakEnergy),sqrtf(castAmplitude.baselineEnergy));
      lastQuietMatch = now;
    }
    if (audioCapture.active && detector.analysisSamples!=previousSamples)
      audioCapture.push(detector.analysisSample);
  }
  static uint32_t lastMatch = 0;
  if (bite && (diagnostic || automatic.state == AutoFishing::Listening) && now - lastMatch > 500) {
    Serial.printf("BITE t=%lu phase=%s elapsed=%lu file=%lu score=%.3f spectrum=%.3f shape=%.3f contrast=%.2f sample=%lu queue_ms=%lu analysis_ms=%lu\n",
      (unsigned long)now, AutoFishing::stateName(automatic.state), (unsigned long)(now - automatic.since),
      (unsigned long)biteReferenceIds[matchedId], matchedScore,
      matchedSpectrum,matchedShape,matchedContrast,(unsigned long)matchedSample,
      (unsigned long)(queuedBytes * 1000u / (4u * FishingAudio::rate.load())),
      (unsigned long)(millis()-now));
    lastMatch = now;
  }
  // Aggregate one-second near-miss diagnostics without recording/exporting audio.
  static uint32_t lastDetectorLog = 0;
  static float bestScore=0, bestSpectrum=0, bestEnvelope=0, bestContrast=0, peakRms=0;
  peakRms=fmaxf(peakRms,detector.lastRms);
  if (detector.lastScore>bestScore) {
    bestScore=detector.lastScore; bestSpectrum=detector.lastSpectrum;
    bestEnvelope=detector.lastEnvelope; bestContrast=detector.lastContrast;
  }
  if (now-lastDetectorLog>=1000) {
    if (diagnostic && autonomousMode)
      Serial.printf("DETECT t=%lu phase=%s peakRms=%.5f best=%.3f spectrum=%.3f shape=%.3f contrast=%.2f\n",
        (unsigned long)now,AutoFishing::stateName(automatic.state),peakRms,
        bestScore,bestSpectrum,bestEnvelope,bestContrast);
    bestScore=bestSpectrum=bestEnvelope=bestContrast=peakRms=0;
    lastDetectorLog=now;
  }
  const uint32_t minimumMs = InputRouter::minimumPressMinMs + esp_random() %
    (InputRouter::minimumPressMaxMs - InputRouter::minimumPressMinMs + 1);
  bool modeChanged = false;
  // Resolve Reel taps on release so a long hold only changes orientation.
  if (!waitingRelease && changedReel && reelButton().down) { reelHeldSince = now; pendingReel = true; }
  if (castButton().down || waitingRelease) pendingReel = false;
  if (reelButton().down && pendingReel && now - reelHeldSince >= 2000) {
    displayFlipped = !displayFlipped;
    uiPreferences.putBool("flipped", displayFlipped);
    screen.setRotation(displayFlipped ? 3 : 1);
    pendingReel = pendingCast = false;
    inputRouter = InputRouter();
    gestureConsumed = true;
    waitingRelease = true; // the held switch now has the other role
    modeChanged = true;
  }
  const bool reelTap = changedReel && !reelButton().down && pendingReel;
  if (changedReel && !reelButton().down) pendingReel = false;
  // Defer Cast until release: a long hold must never send an accidental cast.
  if (!waitingRelease && changedCast && castButton().down) { castHeldSince = now; pendingCast = true; }
  if (!waitingRelease && castButton().down && !reelButton().down && pendingCast && now - castHeldSince >= 2000) {
    automatic.stop(AutoFishing::ModeExit);
    const bool next = !autonomousMode;
    if (FishingAudio::setEnabled(next)) autonomousMode = next;
    else Serial.println("Audio mode switch failed");
    detector.reset();
    while (xStreamBufferReceive(audioBuffer, pcm, sizeof(pcm), 0)) {}
    inputRouter = InputRouter();
    pendingCast = false; gestureConsumed = true; waitingRelease = true;
    modeChanged = true;
  }
  if (castButton().down && reelButton().down) pendingCast = false;
  if (waitingRelease) {
    if (!castButton().down && !reelButton().down) { waitingRelease = false; gestureConsumed = false; }
  } else if (autonomousMode) {
    if (castButton().down && reelButton().down) {
      neutralMode = !neutralMode;
      uiPreferences.putBool("neutral", neutralMode);
      pendingCast = pendingReel = false;
      gestureConsumed = waitingRelease = true;
      inputRouter = InputRouter();
      modeChanged = true;
    } else if (neutralMode && changedCast && !castButton().down && pendingCast) {
      timerStartedAt = now;
      pendingCast = false;
      modeChanged = true;
    } else if (reelTap) {
      if (automatic.running()) { automatic.stop(); castAmplitude.reset(); }
      else if (connected && audioReady && detector.calibrated) {
        detector.reset(); castAmplitude.reset(); automatic.start(now, minimumMs);
      }
      modeChanged = true;
    }
  } else {
    // Feed Cast only on release; retain both-button desk-timer gesture.
    bool castTap = changedCast && !castButton().down && pendingCast && !gestureConsumed;
    if (changedCast && !castButton().down) pendingCast = false;
    bool chord = castButton().down && reelButton().down;
    if (inputRouter.update(now, reelTap || chord, castTap || chord, minimumMs)) {
      neutralMode = !neutralMode;
      uiPreferences.putBool("neutral", neutralMode);
      modeChanged = true;
    }
  }
  auto oldState = automatic.state;
  if (castAmplitude.poll(now)) {
    Serial.printf("CAST_BASELINE t=%lu result=%s rms=%.6f ratio=%.2f\n",
      (unsigned long)now,castAmplitude.state == CastAmplitude::Ready ? "ready" : "invalid",
      sqrtf(castAmplitude.baselineEnergy),CastAmplitude::minimumRatio);
    if (castAmplitude.state != CastAmplitude::Ready) automatic.stop(AutoFishing::CastAudioMissing);
  }
  const uint32_t biteWaitMs = AutoFishing::biteWaitMinMs + esp_random() %
    (AutoFishing::biteWaitMaxMs - AutoFishing::biteWaitMinMs + 1);
  automatic.update(now, bite, autonomousMode && connected && audioReady &&
    detector.calibrated, minimumMs, biteWaitMs);
  if (oldState != automatic.state) {
    if (automatic.state == AutoFishing::Listening) detector.reset();
    // Internal cast/listen/reel transitions do not change the visible layout.
    if ((oldState == AutoFishing::Stopped) != !automatic.running()) modeChanged = true;
  }
  // Log actual state changes, including start/stop paths before update().
  // This separates legitimate bite-driven cycles from no-bite timeouts.
  static AutoFishing::State loggedState = AutoFishing::Stopped;
  static uint32_t stateSince = 0;
  if (automatic.state != loggedState) {
    Serial.printf("AUTO t=%lu %s->%s reason=%s elapsed=%lu bite_wait=%lu BLE=%d audio=%d score=%.3f\n",
      (unsigned long)now, AutoFishing::stateName(loggedState), AutoFishing::stateName(automatic.state),
      AutoFishing::reasonName(automatic.reason), (unsigned long)(now - stateSince),
      (unsigned long)automatic.biteWaitMs, connected, audioReady, detector.lastScore);
    loggedState = automatic.state;
    stateSince = now;
  }
  uint8_t hidOutput = autonomousMode ? automatic.output() : waitingRelease ? 0 : inputRouter.output;
  if (changedConnection && connected) inputRouter.restartReportTiming();
  if (!connected) inputRouter.noteReportSent(now);
  if (connected && (hidOutput != previousHid || changedConnection || now - lastReport >= 100)) {
    pad.send(hidOutput);
    const uint32_t reportSentAt = millis();
    inputRouter.noteReportSent(reportSentAt);
    if (pad.reportQueued) automatic.reportSent(reportSentAt);
    if (pad.reportQueued && autonomousMode && automatic.state == AutoFishing::Cast &&
        automatic.reason == AutoFishing::UserStart && castAmplitude.state == CastAmplitude::Off)
      castAmplitude.begin(reportSentAt);
    static uint32_t lastReportError = 0;
    if (hidOutput != previousHid || changedConnection || (!pad.reportQueued && now-lastReportError>=1000)) {
      Serial.printf("HID t=%lu source=%s phase=%s action=%s output=%u result=%s code=%lu notify_us=%lu auth=%d subscribed=%d\n",
        (unsigned long)millis(),autonomousMode ? "auto" : "manual",AutoFishing::stateName(automatic.state),
        hidOutput==1 ? "reel-X" : hidOutput==2 ? "cast-left" : "release",unsigned(hidOutput),
        pad.reportResult(),(unsigned long)pad.reportCode(),(unsigned long)pad.reportDurationUs,
        pad.authenticated(),pad.notificationsEnabled());
      if (!pad.reportQueued) lastReportError=now;
    }
    previousHid = hidOutput;
    lastReport = now;
  }
  if (!automatic.running()) castAmplitude.reset();
  const uint32_t second = now / 1000;
  if (modeChanged || changedConnection || changedBleStatus) draw(connected);
  else if (neutralMode && (second != displayedSecond ||
      (autonomousMode && (timerCaught != automatic.caught || timerMissed != automatic.missed)))) drawTimer(connected, false);
  else if (!neutralMode) {
    if (changedCast) tile(true, autonomousMode ? "Exit(2s)" : "Cast", castButton().down, TFT_GREEN);
    if (changedReel) tile(false, autonomousMode ? (automatic.running() ? "Stop" : "Start") : "Reel", reelButton().down, TFT_CYAN);
    if (autonomousMode && (lastAutoStatus != autoStatus() ||
        displayedCaught != automatic.caught || displayedMissed != automatic.missed)) drawAutoStatus();
  }
  displayedSecond = second;

  // Keep the validated display fully lit.
  digitalWrite(4, HIGH);
  static uint32_t lastHealth = 0, audioBytes = 0;
  audioBytes += received;
  if (now - lastHealth >= 10000) {
    lastHealth = now;
    Serial.printf("Alive: BLE=%d auth=%d bonds=%d HID=%d audio=%d PCM=%u bytes/10s BL=%d heap=%u\n", connected,
      pad.authenticated(), pad.bondCount(), pad.notificationsEnabled(),
      bool(FishingAudio::streaming), audioBytes, digitalRead(4), ESP.getFreeHeap());
    audioBytes = 0;
  }
  previousConnection = connected;
  delay(2);
}
