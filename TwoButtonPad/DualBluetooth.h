#pragma once
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#include <esp_a2dp_api.h>
#include <esp32-hal-alloc-bt-classic-mem.h>
#include <esp_gap_bt_api.h>
#include <esp_gap_ble_api.h>
#include <atomic>
#include "../libraries/ESP32-BLE-Gamepad/BleXInputDescriptors.h"

namespace FishingBle {
inline constexpr char controllerName[] = "Desk Timer";
inline std::atomic<bool> authenticated{false};

class SecurityCallbacks : public BLESecurityCallbacks {
  bool onSecurityRequest() override { return true; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t result) override {
    authenticated = result.success;
    // Only outcome/mode are logged: never peer addresses, passkeys or bond keys.
    Serial.printf("BLE authentication: success=%d bonded=%d reason=%u\n",
      result.success, bool(result.auth_mode & ESP_LE_AUTH_BOND),
      result.success ? 0u : unsigned(result.fail_reason));
  }
};
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { authenticated = false; }
  void onDisconnect(BLEServer*) override { authenticated = false; }
};
inline SecurityCallbacks securityCallbacks;
inline ServerCallbacks serverCallbacks;
}

// Retain the tested Xbox report map and PnP identity; friendly name is neutral.
class FishingPad {
  BLEServer *server = nullptr;
  BLEHIDDevice *hid = nullptr;
  BLECharacteristic *input = nullptr;
  // Bluedroid reports notification queue acceptance synchronously. This is
  // not an acknowledgement from the phone or game.
  class ReportCallbacks : public BLECharacteristicCallbacks {
  public:
    int status = -1;
    uint32_t code = 0;
    void onStatus(BLECharacteristic*, Status value, uint32_t result) override {
      status = int(value); code = result;
    }
  } reportCallbacks;
  int storedBonds = 0;
  uint32_t lastBondPoll = 0;
public:
  bool begin() {
    if (!BLEDevice::init(FishingBle::controllerName)) return false;
    // Explicitly configure the defaults normally installed by BLESecurity's
    // constructor. Static setters alone did not run that constructor.
    BLESecurity::setKeySize(16);
    BLESecurity::setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    BLESecurity::setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    BLESecurity::setAuthenticationMode(true, false, false);
    BLESecurity::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
    BLESecurity::setCapability(ESP_IO_CAP_NONE);
    BLEDevice::setSecurityCallbacks(&FishingBle::securityCallbacks);
    server = BLEDevice::createServer();
    server->setCallbacks(&FishingBle::serverCallbacks);
    server->advertiseOnDisconnect(true);
    hid = new BLEHIDDevice(server);
    input = hid->inputReport(1);
    input->setCallbacks(&reportCallbacks);
    hid->inputReport(2);
    uint8_t battery = 0;
    hid->inputReport(4)->setValue(&battery, 1);
    hid->outputReport(3);
    hid->reportMap(const_cast<uint8_t*>(XboxOneS_1708_HIDDescriptor), XboxOneS_1708_DescriptorSize);
    // This core requires explicitly creating the optional characteristic first.
    hid->manufacturer()->setValue("DIY Two Button Pad");
    // Arduino 3.3.10's pnp() serializes IDs big-endian. HOGP requires little-
    // endian IDs; write the characteristic explicitly to preserve Xbox identity.
    const uint8_t pnp[] = {2, 0x5e, 0x04, 0xfd, 0x02, 0x08, 0x04};
    hid->deviceInfo()->getCharacteristic(BLEUUID(uint16_t(0x2a50)))->setValue(pnp, sizeof(pnp));
    hid->hidInfo(0, 1);
    hid->startServices();
    hid->setBatteryLevel(100);
    auto adv = BLEDevice::getAdvertising();
    adv->setAppearance(HID_GAMEPAD);
    adv->addServiceUUID(hid->hidService()->getUUID());
    adv->setScanResponse(true);
    BLEAdvertisementData scan;
    scan.setName(FishingBle::controllerName);
    adv->setScanResponseData(scan);
    if (!adv->start()) return false;
    reportBondStatus();
    return true;
  }
  bool isConnected() const { return server && server->getConnectedCount(); }
  bool authenticated() const { return isConnected() && FishingBle::authenticated; }
  int bondCount() const { return storedBonds; }
  void poll(uint32_t now) {
    if (now - lastBondPoll >= 1000) {
      lastBondPoll = now;
      storedBonds = esp_ble_get_bond_device_num();
    }
  }
  bool notificationsEnabled() const {
    if (!input) return false;
    auto cccd = static_cast<BLE2902*>(input->getDescriptorByUUID(BLEUUID(uint16_t(0x2902))));
    return cccd && cccd->getNotifications();
  }
  const char *statusText() const {
    if (isConnected()) return authenticated() ? "BLE Connected" : "BLE Securing...";
    return storedBonds > 0 ? "BLE Paired - waiting" : "BLE Pairing...";
  }
  void reportBondStatus() {
    storedBonds = esp_ble_get_bond_device_num();
    Serial.printf("BLE state: connected=%d authenticated=%d stored_bonds=%d hid_subscribed=%d\n",
      isConnected(), authenticated(), storedBonds, notificationsEnabled());
  }
  bool reportQueued = false;
  uint32_t reportDurationUs = 0;
  const char *reportResult() const {
    switch (reportCallbacks.status) {
      case BLECharacteristicCallbacks::SUCCESS_NOTIFY: return "queued";
      case BLECharacteristicCallbacks::ERROR_NO_CLIENT: return "no-client";
      case BLECharacteristicCallbacks::ERROR_NOTIFY_DISABLED: return "not-subscribed";
      case BLECharacteristicCallbacks::ERROR_GATT: return "gatt-error";
      default: return "no-notify-result";
    }
  }
  uint32_t reportCode() const { return reportCallbacks.code; }
  void send(uint8_t buttons) {
    uint8_t report[16] = {};
    for (int i = 0; i < 8; i += 2) report[i + 1] = 0x80;
    report[12] = (buttons & 2) ? 7 : 0;
    report[13] = (buttons & 1) ? 8 : 0;
    input->setValue(report, sizeof(report));
    reportCallbacks.status = -1; reportCallbacks.code = 0;
    const uint32_t started = micros();
    input->notify();
    reportDurationUs = micros() - started;
    reportQueued = reportCallbacks.status == BLECharacteristicCallbacks::SUCCESS_NOTIFY;
  }
};

// A2DP service lives on the same dual-mode Bluedroid stack. Never tear down
// BLE or release controller memory when disabling the audio profile.
namespace FishingAudio {
inline constexpr char audioName[] = "Desk Audio";
inline std::atomic<bool> enabled{false}, connected{false}, streaming{false};
inline std::atomic<unsigned> rate{44100};
inline std::atomic<bool> transitioning{false};
inline esp_bd_addr_t peer = {};
inline portMUX_TYPE peerMux = portMUX_INITIALIZER_UNLOCKED;
inline void (*pcmConsumer)(const uint8_t*, uint32_t) = nullptr;
inline void event(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *p) {
  if (event == ESP_A2D_PROF_STATE_EVT) {
    transitioning = false;
  } else if (event == ESP_A2D_CONNECTION_STATE_EVT) {
    connected = p->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED;
    if (connected) {
      portENTER_CRITICAL(&peerMux);
      memcpy(peer, p->conn_stat.remote_bda, sizeof(peer));
      portEXIT_CRITICAL(&peerMux);
      if (!enabled) esp_a2d_sink_disconnect(p->conn_stat.remote_bda);
    } else streaming = false;
  } else if (event == ESP_A2D_AUDIO_STATE_EVT) {
    streaming = p->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED;
  } else if (event == ESP_A2D_AUDIO_CFG_EVT) {
    auto f = p->audio_cfg.mcc.cie.sbc[0];
    rate = (f & 0x20) ? 44100 : (f & 0x10) ? 48000 : (f & 0x40) ? 32000 : 16000;
  }
}
inline void pcm(const uint8_t *data, uint32_t length) {
  if (enabled && streaming && pcmConsumer) pcmConsumer(data, length);
}
inline void gap(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *p) {
  if (event == ESP_BT_GAP_CFM_REQ_EVT)
    esp_bt_gap_ssp_confirm_reply(p->cfm_req.bda, enabled);
  else if (event == ESP_BT_GAP_PIN_REQ_EVT) {
    esp_bt_pin_code_t pin = {'0','0','0','0'};
    esp_bt_gap_pin_reply(p->pin_req.bda, enabled, 4, pin);
  }
}
inline bool setEnabled(bool value) {
  if (transitioning) return false;
  if (value == enabled) return true;
  if (value) {
    if (esp_bt_gap_register_callback(gap) != ESP_OK ||
        esp_a2d_register_callback(event) != ESP_OK ||
        esp_a2d_sink_register_data_callback(pcm) != ESP_OK) return false;
    transitioning = true;
    enabled = true;
    if (esp_a2d_sink_init() != ESP_OK) {
      enabled = false; transitioning = false; return false;
    }
    esp_bt_io_cap_t capability = ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &capability, sizeof(capability));
    esp_bt_gap_set_device_name(audioName);
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
  } else {
    enabled = false;
    esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    if (connected) {
      esp_bd_addr_t copy;
      portENTER_CRITICAL(&peerMux);
      memcpy(copy, peer, sizeof(copy));
      portEXIT_CRITICAL(&peerMux);
      esp_a2d_sink_disconnect(copy);
    }
    // Deinit completion is asynchronous; next enable is blocked until settled.
    transitioning = true;
    if (esp_a2d_sink_deinit() != ESP_OK) transitioning = false;
    streaming = false;
    connected = false;
  }
  return true;
}
}
