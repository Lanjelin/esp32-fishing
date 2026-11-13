// Original LilyGO T-Display v1.1 (ESP32, not S3).
#include <Arduino.h>
#include <Preferences.h>
#include <BleGamepad.h>
#include <NimBLEDevice.h>
#include <M5GFX.h>
#include <mbedtls/base64.h>
#include "FishingIcon.h"
#include "InputRouter.h"
#include <lgfx/v1/panel/Panel_ST7789.hpp>
#include <lgfx/v1/platforms/esp32/Bus_SPI.hpp>

BleGamepad pad("Xbox Wireless Controller", "DIY Two Button Pad", 100);
BleGamepadConfiguration config;
// Change these two constants to remap controller inputs (display labels below).
constexpr uint8_t REEL_BUTTON = BUTTON_3;  // Xbox X; BUTTON_1=A, 2=B, 4=Y.
constexpr int8_t CAST_DIRECTION = HAT_LEFT; // HAT_UP / DOWN / LEFT / RIGHT.

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
Button xButton{0}, leftButton{35};
bool previousConnection = false;
uint32_t lastActivity = 0, lastReport = 0;
Preferences uiPreferences;
bool neutralMode = false;
InputRouter inputRouter;
uint8_t previousHid = 0;
uint32_t displayedSecond = UINT32_MAX;

void drawTimer(bool connected, bool fullRefresh = true) {
  const uint16_t foreground = canvas->color565(180, 187, 191);
  if (fullRefresh) {
    canvas->fillScreen(TFT_BLACK);
    canvas->setTextColor(foreground, TFT_BLACK);
    canvas->drawCentreString("DESK TIMER", 120, 12, 2);
    canvas->setTextColor(canvas->color565(91, 100, 105), TFT_BLACK);
    canvas->drawCentreString(connected ? "Bluetooth connected" : "Bluetooth ready", 120, 111, 2);
  }
  uint32_t elapsed = millis() / 1000;
  char timer[12];
  snprintf(timer, sizeof(timer), "%02lu:%02lu:%02lu",
    (unsigned long)((elapsed / 3600) % 100),
    (unsigned long)((elapsed / 60) % 60), (unsigned long)(elapsed % 60));
  canvas->setTextColor(foreground, TFT_BLACK);
  canvas->setTextPadding(220);
  canvas->drawCentreString(timer, 120, 45, 6);
  canvas->setTextPadding(0);
}


void tile(int y, const char *label, bool pressed, uint16_t accent) {
  const uint16_t background = pressed ? accent : canvas->color565(20, 29, 36);
  canvas->fillRoundRect(124, y, 108, 38, 7, background);
  canvas->drawRoundRect(124, y, 108, 38, 7, accent);
  canvas->setTextColor(pressed ? TFT_BLACK : TFT_WHITE, background);
  canvas->drawCentreString(label, 178, y + 7, 4);
}
void draw(bool connected) {
  if (neutralMode) { drawTimer(connected); return; }
  canvas->fillScreen(TFT_BLACK);
  canvas->setTextColor(canvas->color565(239, 201, 112), TFT_BLACK);
  canvas->drawCentreString("Two Button Fishing Pad", 120, 3, 2);
  canvas->setTextColor(connected ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
  canvas->drawString(connected ? "BLE Connected" : "BLE Pairing...", 8, 24, 2);
  canvas->drawFastHLine(8, 43, 224, canvas->color565(69, 56, 32));
  // Original WoW Fishing profession icon, framed in warm gold.
  canvas->fillRoundRect(16, 49, 80, 80, 8, canvas->color565(25, 21, 15));
  canvas->drawRoundRect(16, 49, 80, 80, 8, canvas->color565(189, 148, 67));
  canvas->drawRoundRect(19, 52, 74, 74, 6, canvas->color565(91, 66, 32));
  canvas->drawJpg(fishingIconJpg, sizeof(fishingIconJpg), 28, 61);
  tile(49, "Cast", leftButton.down, TFT_GREEN);
  tile(93, "Reel", xButton.down, TFT_CYAN);
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
  screen.setRotation(1);
  // Full brightness; keep the validated backlight setup.
  pinMode(4, OUTPUT);
  digitalWrite(4, HIGH);
  Serial.println("Display: M5GFX ST7789 135x240, mode0 20MHz, GPIO18/19/5/16/23 BL4");
  uiPreferences.begin("fishing-ui", false);
  neutralMode = uiPreferences.getBool("neutral", false);
  draw(false);
  config.setGamepadMode(GamepadMode::XInputOneS);
  config.setAutoReport(false);
  // Initialize synchronously and check failure before the library creates GATT objects.
  if (!NimBLEDevice::init("Xbox Wireless Controller")) {
    Serial.println("BLE initialization FAILED");
    screen.fillScreen(TFT_BLACK);
    screen.setTextColor(TFT_RED, TFT_BLACK);
    screen.drawString("BLE init failed", 10, 40, 2);
    while (true) delay(1000);
  }
  pad.begin(&config);
  pad.setLeftThumb(0, 0);
  pad.setRightThumb(0, 0);
  pad.setLeftTrigger(0);
  pad.setRightTrigger(0);
  pad.setHat1(HAT_CENTERED);
  Serial.println("Fishing pad ready: GPIO35=Cast (Left), GPIO0=Reel (X).");
  lastActivity = millis();
}
void loop() {
  if (Serial.available()) {
    char command = Serial.read();
    if (command == 's') screenshot();
    else if (command == 't') screenshot(true);
  }
  uint32_t now = millis();
  bool changedX = xButton.poll(now);
  bool changedLeft = leftButton.poll(now);
  bool connected = pad.isConnected();
  bool changedConnection = connected != previousConnection;
  // Consume the mode-switch gesture before building any HID report.
  bool modeChanged = inputRouter.update(now, xButton.down, leftButton.down);
  if (modeChanged) {
    neutralMode = !neutralMode;
    uiPreferences.putBool("neutral", neutralMode);
    Serial.printf("Display mode: %s\n", neutralMode ? "neutral" : "fishing");
  }
  const uint32_t second = now / 1000;
  if (modeChanged || (neutralMode && (second != displayedSecond || changedConnection))) {
    if (neutralMode && !modeChanged && !changedConnection) drawTimer(connected, false);
    else draw(connected);
    displayedSecond = second;
  }
  if (changedX || changedLeft || changedConnection) {
    lastActivity = now;
    Serial.printf("BLE=%d X=%d LEFT=%d\n", connected, xButton.down, leftButton.down);
    if (!neutralMode && changedConnection) draw(connected);
    else if (!neutralMode && !modeChanged) {
      if (changedLeft) tile(49, "Cast", leftButton.down, TFT_GREEN);
      if (changedX) tile(93, "Reel", xButton.down, TFT_CYAN);
    }
  }
  // Refresh the complete state on reconnect and periodically: no stale held inputs.
  if (connected && (inputRouter.output != previousHid || changedConnection || now - lastReport >= 100)) {
    if (inputRouter.output & 1) pad.press(REEL_BUTTON); else pad.release(REEL_BUTTON);
    pad.setHat1((inputRouter.output & 2) ? CAST_DIRECTION : HAT_CENTERED);
    pad.sendReport();
    previousHid = inputRouter.output;
    lastReport = now;
  }
  // Keep the validated display fully lit.
  digitalWrite(4, HIGH);
  static uint32_t lastHealth = 0;
  if (now - lastHealth >= 10000) {
    lastHealth = now;
    Serial.printf("Alive: BLE=%d advertising=%d BL=%d heap=%u\n", connected,
      NimBLEDevice::getAdvertising()->isAdvertising(), digitalRead(4), ESP.getFreeHeap());
  }
  previousConnection = connected;
  delay(2);
}
