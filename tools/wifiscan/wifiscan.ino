#include <WiFi.h>
#include <esp_wifi.h>

// Fill in the network under test; this sketch is a link-debugging aid, not
// part of the firmware.
static const char* ssid = "YOUR_SSID";
static const char* pass = "YOUR_PASSWORD";
static volatile int lastReason = -1;

static const char* authName(wifi_auth_mode_t m) {
  switch (m) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENT";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    default: return "OTHER";
  }
}

void onEv(WiFiEvent_t e, WiFiEventInfo_t info) {
  if (e == ARDUINO_EVENT_WIFI_STA_CONNECTED)
    Serial.printf("   assoc ok ch %d\n", info.wifi_sta_connected.channel);
  else if (e == ARDUINO_EVENT_WIFI_STA_GOT_IP)
    Serial.printf("   GOT IP %s\n", WiFi.localIP().toString().c_str());
  else if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    lastReason = info.wifi_sta_disconnected.reason;
    Serial.printf("   disc reason %d\n", lastReason);
  }
}

static bool attempt(bool pmf, uint32_t ms) {
  wifi_config_t cfg = {};
  strncpy((char*)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
  strncpy((char*)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
  cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  cfg.sta.pmf_cfg.capable = pmf;
  cfg.sta.pmf_cfg.required = false;
  lastReason = -1;
  Serial.printf(" try pmf=%d\n", (int)pmf);
  esp_wifi_disconnect();
  delay(300);
  esp_wifi_set_config(WIFI_IF_STA, &cfg);
  esp_wifi_connect();
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    delay(250);
    if (WiFi.status() == WL_CONNECTED) return true;
  }
  Serial.printf(" result status %d reason %d\n", WiFi.status(), lastReason);
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(400);
  WiFi.onEvent(onEv);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  delay(300);

  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++)
    if (WiFi.SSID(i) == ssid)
      Serial.printf("scan: %s rssi %d ch %d %s bssid %s\n", WiFi.SSID(i).c_str(),
                    WiFi.RSSI(i), WiFi.channel(i),
                    authName(WiFi.encryptionType(i)),
                    WiFi.BSSIDstr(i).c_str());

  if (attempt(false, 12000)) return;
  if (attempt(true, 12000)) return;
  Serial.println("both attempts failed");
}

void loop() { delay(1000); }
