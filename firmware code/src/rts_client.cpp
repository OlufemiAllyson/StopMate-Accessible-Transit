#include "rts_client.h"
#include "rts_predictions.h"
#include "rts_root_ca.h"
#include "secrets.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <memory>
#include <time.h>

namespace {
constexpr uint32_t WIFI_TIMEOUT_MS = 20000;
constexpr uint32_t WIFI_RETRY_MS = 30000;
constexpr uint32_t CLOCK_TIMEOUT_MS = 15000;
constexpr size_t MAX_RESPONSE_BYTES = 32768;

bool networkInitialized = false;
bool networkStarted = false;
bool wasConnected = false;
bool clockStarted = false;
uint32_t lastConnectAttempt = 0;

// HTTPClient handles chunked responses; this sink bounds their memory usage.
class ResponseBuffer : public Stream {
 public:
  std::unique_ptr<char[]> data{new (std::nothrow) char[MAX_RESPONSE_BYTES + 1]};
  size_t length = 0;
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t *bytes, size_t count) override {
    if (!data || count > MAX_RESPONSE_BYTES - length) return 0;
    memcpy(data.get() + length, bytes, count);
    length += count;
    data[length] = '\0';
    return count;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
};

String urlEncode(const char *text) {
  String encoded;
  const char hex[] = "0123456789ABCDEF";
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p) {
    const unsigned char c = *p;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += static_cast<char>(c);
    } else {
      encoded += '%';
      encoded += hex[c >> 4];
      encoded += hex[c & 15];
    }
  }
  return encoded;
}

bool connectNetwork() {
  beginNetwork();
  if (!networkStarted || !RTS_API_KEY[0]) {
    Serial.println("Set WIFI_SSID, WIFI_PASSWORD, and RTS_API_KEY in include/secrets.h, then upload firmware.");
    return false;
  }
  serviceNetwork();
  if (WiFi.status() != WL_CONNECTED) Serial.println("Waiting for Wi-Fi for the RTS lookup...");
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
    serviceNetwork();
    delay(100);
  }
  serviceNetwork();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi lookup wait timed out; background reconnection remains active.");
    return false;
  }
  // Certificate validity needs a real clock. Countdown values do not need timezone conversion.
  const uint32_t clockStart = millis();
  while (time(nullptr) < 1767225600 && millis() - clockStart < CLOCK_TIMEOUT_MS) delay(100);
  if (time(nullptr) < 1767225600) {
    Serial.println("Clock synchronization failed; refusing an unverified HTTPS connection.");
    return false;
  }
  return true;
}

bool fetchArrivals(ArrivalSnapshot &snapshot, uint32_t &requestedAt) {
  WiFiClientSecure tls;
  tls.setCACert(RTS_ROOT_CA);
  tls.setHandshakeTimeout(12);
  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  const String url = String(RTS_PREDICTIONS_URL) + "?format=json&stpid=" +
                     urlEncode(RTS_STOP_ID) + "&top=20&key=" + urlEncode(RTS_API_KEY);
  // Never log this URL: the BusTime protocol puts its key in the query string.
  if (!http.begin(tls, url)) {
    Serial.println("Could not initialize the RTS HTTPS request.");
    return false;
  }
  requestedAt = millis();
  const int status = http.GET();  // One API request per explicit announcement call.
  if (status != HTTP_CODE_OK) {
    Serial.printf("RTS request failed (HTTP/transport status %d).\n", status);
    http.end();
    return false;
  }
  if (http.getSize() > static_cast<int>(MAX_RESPONSE_BYTES)) {
    Serial.println("RTS response exceeds the size limit.");
    http.end();
    return false;
  }
  ResponseBuffer body;
  const int received = body.data ? http.writeToStream(&body) : -1;
  http.end();
  if (received <= 0 || static_cast<size_t>(received) != body.length) {
    Serial.println("RTS response was empty, incomplete, or too large.");
    return false;
  }
  JsonDocument json;
  const auto error = deserializeJson(json, body.data.get(), body.length,
                                     DeserializationOption::NestingLimit(8));
  if (error) {
    Serial.printf("Invalid RTS JSON: %s\n", error.c_str());
    return false;
  }
  JsonObjectConst response = json["bustime-response"].as<JsonObjectConst>();
  if (response.isNull()) {
    Serial.println("RTS response is missing bustime-response.");
    return false;
  }
  if (!parseArrivalResponse(response, RTS_STOP_ID, snapshot)) {
    // Do not print server error strings, which might echo request credentials.
    Serial.println("RTS response unavailable, invalid, or contains only unsupported predictions.");
    return false;
  }
  Serial.printf("RTS snapshot: %u route(s) to announce; %u unsupported/delayed predictions skipped.\n",
                static_cast<unsigned>(snapshot.count), static_cast<unsigned>(snapshot.skipped));
  return true;
}
}

void beginNetwork() {
  if (networkInitialized) return;
  networkInitialized = true;
  if (!WIFI_SSID[0]) {
    Serial.println("Wi-Fi is not configured; set include/secrets.h and upload firmware.");
    return;
  }
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  lastConnectAttempt = millis();
  networkStarted = true;
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Wi-Fi started; keeping the connection active between announcements.");
}

void serviceNetwork() {
  if (!networkStarted) return;
  const bool connected = WiFi.status() == WL_CONNECTED;
  const uint32_t now = millis();
  if (connected) {
    if (!wasConnected) Serial.println("Wi-Fi connected; automatic reconnection enabled.");
    if (!clockStarted) {
      configTime(0, 0, "pool.ntp.org", "time.google.com");
      clockStarted = true;
    }
  } else {
    if (wasConnected) {
      Serial.println("Wi-Fi connection lost; reconnecting in the background.");
      lastConnectAttempt = now;
    }
    // Auto-reconnect handles disconnect events. Also retry startup failures or
    // an absent hotspot without blocking recognition or fetching bus data.
    if (now - lastConnectAttempt >= WIFI_RETRY_MS) {
      lastConnectAttempt = now;
      Serial.println("Retrying Wi-Fi connection in the background...");
      WiFi.reconnect();
    }
  }
  wasConnected = connected;
}

void announceArrivals() {
  ArrivalSnapshot snapshot;
  uint32_t requestedAt = 0;
  // fetchArrivals releases its HTTP/TLS objects; the Wi-Fi connection stays up.
  if (!connectNetwork() || !fetchArrivals(snapshot, requestedAt)) return;
  if (!snapshot.count) {
    Serial.printf("No buses available at stop %s.\n", RTS_STOP_ID);
    if (!playNoBusesAvailable()) Serial.println("Could not play the no-buses announcement.");
    return;
  }
  for (size_t i = 0; i < snapshot.count; ++i) {
    const Arrival &arrival = snapshot.arrivals[i];
    // Avoid announcing an ETA that expired while connecting/downloading/playing.
    const uint32_t elapsedSeconds = (millis() - requestedAt) / 1000;
    if (elapsedSeconds > 120) {
      Serial.println("Remaining snapshot is stale; skipping it without another request.");
      break;
    }
    const int secondsLeft = arrival.minutes * 60 - static_cast<int>(elapsedSeconds);
    // DUE/zero remains an arriving-now announcement, never a fabricated minute.
    if (arrival.minutes > 0 && secondsLeft < -60) continue;
    const int minutesLeft = secondsLeft <= 0 ? 0 : (secondsLeft + 59) / 60;
    const auto &route = MONITORED_ROUTES[arrival.routeIndex];
    if (!playAnnouncement(route.spokenNumber, minutesLeft, route.audioRoute)) {
      Serial.printf("Could not announce route %s.\n", route.apiRoute);
    }
  }
  Serial.println("Arrivals complete. Press the button for another announcement.");
}
