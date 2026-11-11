#include "net.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <time.h>
#include "secrets.h"

static WiFiClientSecure gTls;
static HTTPClient gHttp;
static String gHost;
static uint32_t gLastMs = 0;

static String hostOf(const String& url) {
  int a = url.indexOf("://");
  if (a < 0) return "";
  int b = url.indexOf('/', a + 3);
  return url.substring(a + 3, b < 0 ? (int)url.length() : b);
}

bool net::connect(uint32_t timeoutMs) {
  if (strlen(WIFI_SSID) == 0) return false;

  WiFi.persistent(false);
  // Drop any half-open attempt from a previous timeout, otherwise the stack
  // answers "sta is connecting, cannot set config" and never retries.
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    delay(200);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("[net] wifi failed, status %d\n", WiFi.status());
    return false;
  }
  // A fresh association invalidates any kept-alive TLS session
  gHttp.end();
  gTls.stop();
  gHost = "";
  Serial.printf("[net] wifi %s rssi %d\n", WiFi.localIP().toString().c_str(),
                WiFi.RSSI());

  configTzTime("CST-8", "ntp.aliyun.com", "cn.pool.ntp.org",
               "time.cloudflare.com");
  t0 = millis();
  while (time(nullptr) < 1700000000 && millis() - t0 < 15000) delay(200);
  Serial.printf("[net] time %ld\n", (long)time(nullptr));
  return true;
}

bool net::online() { return WiFi.status() == WL_CONNECTED; }

bool net::timeReady() { return time(nullptr) > 1700000000; }

static int getRaw(const String& url, Stream** body, uint32_t timeoutMs,
                  bool reuse) {
  *body = nullptr;
  // LWIP asserts on a NULL tcpip semaphore if we touch sockets before WiFi is up
  if (!net::online()) return -1;
  String host = hostOf(url);
  if (host != gHost) {
    gHttp.end();
    gTls.stop();
    gHost = host;
  }
  gTls.setInsecure();
  gTls.setHandshakeTimeout(15);
  gHttp.setReuse(reuse);
  gHttp.setTimeout(timeoutMs);
  gHttp.setConnectTimeout(10000);
  if (!gHttp.begin(gTls, url)) return -1;

  uint32_t t0 = millis();
  int code = gHttp.GET();
  gLastMs = millis() - t0;
  *body = gHttp.getStreamPtr();
  if (code < 0)
    Serial.printf("[net] %s code %d after %u ms\n", host.c_str(), code,
                  (unsigned)gLastMs);
  return code;
}

// Some hosts answer with Transfer-Encoding: chunked, which getStreamPtr()
// does not decode; writeToStream() handles both framing styles.
static const size_t kMaxBody = 320 * 1024;

class BodySink : public Stream {
 public:
  explicit BodySink(net::Body& b) : _b(b) {}
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* p, size_t n) override {
    if (_b.len + n > kMaxBody) return 0;
    if (_b.len + n > _b.cap) {
      size_t nc = _b.cap ? _b.cap : 8192;
      while (nc < _b.len + n) nc *= 2;
      if (nc > kMaxBody) nc = kMaxBody;
      uint8_t* p2 = (uint8_t*)heap_caps_realloc(_b.data, nc,
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!p2) p2 = (uint8_t*)realloc(_b.data, nc);
      if (!p2) return 0;
      _b.data = p2;
      _b.cap = nc;
    }
    memcpy(_b.data + _b.len, p, n);
    _b.len += n;
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }

 private:
  net::Body& _b;
};

int net::getBody(const String& url, Body& out, uint32_t timeoutMs) {
  out.release();
  Stream* body = nullptr;
  int code = getRaw(url, &body, timeoutMs, false);
  if (code != 200 || !body) {
    gHttp.end();
    return code;
  }
  BodySink sink(out);
  int n = gHttp.writeToStream(&sink);
  gHttp.end();
  if (n < 0) {
    out.release();
    return n;
  }
  return 200;
}

