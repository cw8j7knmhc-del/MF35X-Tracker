#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <LittleFS.h>
#include <string.h>
#include <stddef.h>

// ==================================================
// NAECHSTER USB-STAND - PUNKT 12
// WLAN / ROUTER / DNS / FIREBASE / OPTIONAL RUT200-LTE
// ==================================================
// Ziel: Bei einem Ausfall eindeutig unterscheiden koennen zwischen
// - ESP32 <-> RUT200 WLAN
// - Erreichbarkeit des lokalen Routers/Gateways
// - DNS-Aufloesung
// - HTTPS/Firebase
// - Mobilfunkqualitaet des RUT200 (optional ueber dessen lokale RutOS API)
//
// RUT200-Zugangsdaten werden NIE im Repository gespeichert. Optional lokal
// in secrets.h definieren:
//   #define MF35X_RUT200_USERNAME "admin"
//   #define MF35X_RUT200_PASSWORD "..."
// Ohne Passwort funktionieren WLAN/Gateway/DNS/Firebase-Diagnosen trotzdem.
// ==================================================

#ifndef MF35X_RUT200_USERNAME
#define MF35X_RUT200_USERNAME "admin"
#endif
#ifndef MF35X_RUT200_PASSWORD
#define MF35X_RUT200_PASSWORD ""
#endif

constexpr unsigned long MF35X_NET_PROBE_INTERVAL_MS = 15000UL;
constexpr uint16_t MF35X_NET_TCP_TIMEOUT_MS = 700U;
constexpr uint16_t MF35X_NET_HTTP_TIMEOUT_MS = 3000U;
constexpr uint32_t MF35X_NET_TASK_STACK = 7168;
constexpr UBaseType_t MF35X_NET_TASK_PRIORITY = 1;
constexpr BaseType_t MF35X_NET_TASK_CORE = 0;
constexpr TickType_t MF35X_NET_TASK_IDLE = pdMS_TO_TICKS(250);
constexpr size_t MF35X_NET_FLASH_PROTECT_BYTES = 896UL * 1024UL;
constexpr unsigned long MF35X_NET_DRAIN_MS = 350UL;
constexpr uint32_t MF35X_NET_MAGIC = 0x4D464E31UL; // "MFN1"
constexpr uint16_t MF35X_NET_VERSION = 1;
constexpr uint8_t MF35X_NET_PENDING = 0xA5;
constexpr uint8_t MF35X_NET_SENT = 0x5A;

constexpr uint16_t MF35X_NET_WIFI_CONNECTED = 1u << 0;
constexpr uint16_t MF35X_NET_GATEWAY_OK = 1u << 1;
constexpr uint16_t MF35X_NET_DNS_OK = 1u << 2;
constexpr uint16_t MF35X_NET_FIREBASE_OK = 1u << 3;
constexpr uint16_t MF35X_NET_RUT_CONFIGURED = 1u << 4;
constexpr uint16_t MF35X_NET_RUT_API_OK = 1u << 5;
constexpr uint16_t MF35X_NET_LTE_RSSI_VALID = 1u << 6;
constexpr uint16_t MF35X_NET_LTE_RSRP_VALID = 1u << 7;
constexpr uint16_t MF35X_NET_LTE_RSRQ_VALID = 1u << 8;
constexpr uint16_t MF35X_NET_LTE_SINR_VALID = 1u << 9;

struct Mf35xConnectivitySnapshot {
  uint32_t updatedMillis;
  uint16_t flags;
  int16_t wifiRssi;
  uint16_t gatewayLatencyMs;
  uint16_t dnsLatencyMs;
  uint16_t firebaseLatencyMs;
  int16_t firebaseHttpCode;
  uint16_t consecutiveFailures;
  uint32_t wifiReconnects;
  uint32_t internetFailures;
  int16_t lteRssi;
  int16_t lteRsrp;
  int16_t lteRsrqDeci;
  int16_t lteSinrDeci;
  char operatorName[20];
  char networkType[12];
};

#pragma pack(push, 1)
struct Mf35xNetDiagRecord {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t bootId;
  uint32_t sequence;
  uint16_t flags;
  int16_t wifiRssi;
  uint16_t gatewayLatencyMs;
  uint16_t dnsLatencyMs;
  uint16_t firebaseLatencyMs;
  int16_t firebaseHttpCode;
  uint16_t consecutiveFailures;
  uint32_t wifiReconnects;
  uint32_t internetFailures;
  uint16_t probeAgeMs;
  int16_t lteRssi;
  int16_t lteRsrp;
  int16_t lteRsrqDeci;
  int16_t lteSinrDeci;
  char operatorName[20];
  char networkType[12];
  uint32_t crc32;
  uint8_t state;
  uint8_t padding[3];
};
#pragma pack(pop)

portMUX_TYPE mf35xConnectivityMux = portMUX_INITIALIZER_UNLOCKED;
Mf35xConnectivitySnapshot mf35xConnectivityCurrent = {};
TaskHandle_t mf35xConnectivityTaskHandle = nullptr;
String mf35xRutToken = "";
unsigned long mf35xRutTokenValidUntil = 0;
unsigned long mf35xNetLastDrainMs = 0;
uint32_t mf35xNetDiagQueued = 0;
uint32_t mf35xNetDiagReplayed = 0;
uint32_t mf35xNetDiagDropped = 0;

uint16_t mf35xNetElapsedMs(uint32_t startMs) {
  const uint32_t dt = (uint32_t)(millis() - startMs);
  return dt > 65535UL ? 65535U : (uint16_t)dt;
}

Mf35xConnectivitySnapshot mf35xConnectivitySnapshotLesen() {
  Mf35xConnectivitySnapshot copy = {};
  portENTER_CRITICAL(&mf35xConnectivityMux);
  copy = mf35xConnectivityCurrent;
  portEXIT_CRITICAL(&mf35xConnectivityMux);
  return copy;
}

void mf35xConnectivitySnapshotSchreiben(const Mf35xConnectivitySnapshot& value) {
  portENTER_CRITICAL(&mf35xConnectivityMux);
  mf35xConnectivityCurrent = value;
  portEXIT_CRITICAL(&mf35xConnectivityMux);
}

bool mf35xNetJsonNumber(const String& json, const char* key, double& value) {
  if (jsonZahl(json, key, value)) return true;
  String text;
  if (!jsonString(json, key, text) || text.length() == 0) return false;
  char* end = nullptr;
  const double parsed = strtod(text.c_str(), &end);
  if (end == text.c_str()) return false;
  value = parsed;
  return isfinite(value);
}

bool mf35xNetJsonNumberAny(
  const String& json,
  const char* a,
  const char* b,
  const char* c,
  double& value
) {
  return (a && mf35xNetJsonNumber(json, a, value)) ||
         (b && mf35xNetJsonNumber(json, b, value)) ||
         (c && mf35xNetJsonNumber(json, c, value));
}

bool mf35xNetJsonStringAny(
  const String& json,
  const char* a,
  const char* b,
  const char* c,
  String& value
) {
  return (a && jsonString(json, a, value)) ||
         (b && jsonString(json, b, value)) ||
         (c && jsonString(json, c, value));
}

bool mf35xGatewayProbe(uint16_t& latencyMs) {
  latencyMs = 0;
  if (WiFi.status() != WL_CONNECTED) return false;
  const IPAddress gateway = WiFi.gatewayIP();
  if ((uint32_t)gateway == 0) return false;

  const uint32_t start = millis();
  WiFiClient client;
  bool ok = client.connect(gateway, 443, MF35X_NET_TCP_TIMEOUT_MS);
  if (!ok) {
    client.stop();
    ok = client.connect(gateway, 80, MF35X_NET_TCP_TIMEOUT_MS);
  }
  latencyMs = mf35xNetElapsedMs(start);
  client.stop();
  return ok;
}

bool mf35xDnsProbe(uint16_t& latencyMs) {
  latencyMs = 0;
  if (WiFi.status() != WL_CONNECTED) return false;
  const uint32_t start = millis();
  IPAddress resolved;
  const int ok = WiFi.hostByName(
    "tracker-989a9-default-rtdb.europe-west1.firebasedatabase.app",
    resolved
  );
  latencyMs = mf35xNetElapsedMs(start);
  return ok == 1 && (uint32_t)resolved != 0;
}

bool mf35xFirebaseProbe(uint16_t& latencyMs, int16_t& httpCode) {
  latencyMs = 0;
  httpCode = 0;
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(MF35X_NET_HTTP_TIMEOUT_MS);

  String url = String(FIREBASE_ROOT) + "/tracker/device.json?shallow=true";
  const uint32_t start = millis();
  if (!http.begin(client, url)) {
    latencyMs = mf35xNetElapsedMs(start);
    httpCode = -1;
    return false;
  }

  const int code = http.GET();
  latencyMs = mf35xNetElapsedMs(start);
  httpCode = code < INT16_MIN ? INT16_MIN : code > INT16_MAX ? INT16_MAX : (int16_t)code;
  if (code > 0) {
    // Body bewusst nicht einlesen; fuer die Diagnose reicht der HTTP-Status.
  }
  http.end();
  return code >= 200 && code < 300;
}

String mf35xRutBaseUrl() {
  return String("https://") + WiFi.gatewayIP().toString() + "/api";
}

bool mf35xRutLogin() {
  mf35xRutToken = "";
  mf35xRutTokenValidUntil = 0;
  if (strlen(MF35X_RUT200_PASSWORD) == 0 || WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(MF35X_NET_HTTP_TIMEOUT_MS);
  if (!http.begin(client, mf35xRutBaseUrl() + "/login")) return false;
  http.addHeader("Content-Type", "application/json");

  String body = "{\"username\":\"" + jsonEscape(String(MF35X_RUT200_USERNAME)) +
                "\",\"password\":\"" + jsonEscape(String(MF35X_RUT200_PASSWORD)) + "\"}";
  const int code = http.POST(body);
  const String response = code >= 200 && code < 300 ? http.getString() : "";
  http.end();
  if (code < 200 || code >= 300) return false;

  String token;
  if (!jsonString(response, "token", token) || token.length() < 10) return false;
  mf35xRutToken = token;
  mf35xRutTokenValidUntil = millis() + 240000UL;
  return true;
}

bool mf35xRutGet(const char* endpoint, String& response) {
  response = "";
  if (strlen(MF35X_RUT200_PASSWORD) == 0 || WiFi.status() != WL_CONNECTED) return false;
  if (mf35xRutToken.length() == 0 ||
      (int32_t)(millis() - mf35xRutTokenValidUntil) >= 0) {
    if (!mf35xRutLogin()) return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(MF35X_NET_HTTP_TIMEOUT_MS);
  if (!http.begin(client, mf35xRutBaseUrl() + endpoint)) return false;
  http.addHeader("Authorization", "Bearer " + mf35xRutToken);
  const int code = http.GET();
  if (code >= 200 && code < 300) response = http.getString();
  http.end();

  if (code == 401 || code == 403) {
    mf35xRutToken = "";
    mf35xRutTokenValidUntil = 0;
  }
  return code >= 200 && code < 300;
}

void mf35xRutMetricsLesen(Mf35xConnectivitySnapshot& out) {
  if (strlen(MF35X_RUT200_PASSWORD) == 0) return;
  out.flags |= MF35X_NET_RUT_CONFIGURED;

  String signalJson;
  if (!mf35xRutGet("/modems/signal/status", signalJson)) return;
  out.flags |= MF35X_NET_RUT_API_OK;

  double value = 0.0;
  if (mf35xNetJsonNumberAny(signalJson, "rssi", "signal", "signal_strength", value)) {
    out.lteRssi = (int16_t)lround(value);
    out.flags |= MF35X_NET_LTE_RSSI_VALID;
  }
  if (mf35xNetJsonNumberAny(signalJson, "rsrp", "lte_rsrp", nullptr, value)) {
    out.lteRsrp = (int16_t)lround(value);
    out.flags |= MF35X_NET_LTE_RSRP_VALID;
  }
  if (mf35xNetJsonNumberAny(signalJson, "rsrq", "lte_rsrq", nullptr, value)) {
    out.lteRsrqDeci = (int16_t)lround(value * 10.0);
    out.flags |= MF35X_NET_LTE_RSRQ_VALID;
  }
  if (mf35xNetJsonNumberAny(signalJson, "sinr", "snr", "lte_sinr", value)) {
    out.lteSinrDeci = (int16_t)lround(value * 10.0);
    out.flags |= MF35X_NET_LTE_SINR_VALID;
  }

  String statusJson;
  if (!mf35xRutGet("/modems/status", statusJson)) return;

  String text;
  if (mf35xNetJsonStringAny(statusJson, "operator", "operator_name", "network_operator", text)) {
    strncpy(out.operatorName, text.c_str(), sizeof(out.operatorName) - 1);
  }
  text = "";
  if (mf35xNetJsonStringAny(statusJson, "network", "network_type", "connection_type", text)) {
    strncpy(out.networkType, text.c_str(), sizeof(out.networkType) - 1);
  }
}

String mf35xConnectivityJson(const Mf35xConnectivitySnapshot& s) {
  String json = "{";
  json.reserve(760);
  bool first = true;
  jsonBoolFeld(json, first, "wifi_connected", (s.flags & MF35X_NET_WIFI_CONNECTED) != 0);
  jsonRaw(json, first, "wifi_rssi_dbm", String(s.wifiRssi));
  jsonBoolFeld(json, first, "gateway_reachable", (s.flags & MF35X_NET_GATEWAY_OK) != 0);
  jsonULongFeld(json, first, "gateway_latency_ms", s.gatewayLatencyMs);
  jsonBoolFeld(json, first, "dns_ok", (s.flags & MF35X_NET_DNS_OK) != 0);
  jsonULongFeld(json, first, "dns_latency_ms", s.dnsLatencyMs);
  jsonBoolFeld(json, first, "firebase_ok", (s.flags & MF35X_NET_FIREBASE_OK) != 0);
  jsonLongFeld(json, first, "firebase_http_code", s.firebaseHttpCode);
  jsonULongFeld(json, first, "firebase_latency_ms", s.firebaseLatencyMs);
  jsonULongFeld(json, first, "failures_consecutive", s.consecutiveFailures);
  jsonULongFeld(json, first, "wifi_reconnects", s.wifiReconnects);
  jsonULongFeld(json, first, "internet_failures", s.internetFailures);
  jsonBoolFeld(json, first, "rut200_api_configured", (s.flags & MF35X_NET_RUT_CONFIGURED) != 0);
  jsonBoolFeld(json, first, "rut200_api_ok", (s.flags & MF35X_NET_RUT_API_OK) != 0);

  if (s.flags & MF35X_NET_LTE_RSSI_VALID) jsonLongFeld(json, first, "lte_rssi_dbm", s.lteRssi);
  else jsonRaw(json, first, "lte_rssi_dbm", "null");
  if (s.flags & MF35X_NET_LTE_RSRP_VALID) jsonLongFeld(json, first, "lte_rsrp_dbm", s.lteRsrp);
  else jsonRaw(json, first, "lte_rsrp_dbm", "null");
  if (s.flags & MF35X_NET_LTE_RSRQ_VALID) jsonFloatFeld(json, first, "lte_rsrq_db", (double)s.lteRsrqDeci / 10.0, 1);
  else jsonRaw(json, first, "lte_rsrq_db", "null");
  if (s.flags & MF35X_NET_LTE_SINR_VALID) jsonFloatFeld(json, first, "lte_sinr_db", (double)s.lteSinrDeci / 10.0, 1);
  else jsonRaw(json, first, "lte_sinr_db", "null");

  jsonText(json, first, "lte_operator", String(s.operatorName));
  jsonText(json, first, "lte_network_type", String(s.networkType));
  jsonULongFeld(json, first, "updated_uptime_ms", s.updatedMillis);
  json += '}';
  return json;
}

void mf35xConnectivityProbe() {
  const Mf35xConnectivitySnapshot old = mf35xConnectivitySnapshotLesen();
  Mf35xConnectivitySnapshot next = {};
  next.wifiReconnects = old.wifiReconnects;
  next.internetFailures = old.internetFailures;
  next.consecutiveFailures = old.consecutiveFailures;
  next.wifiRssi = -127;

  const bool oldWifi = (old.flags & MF35X_NET_WIFI_CONNECTED) != 0;
  const bool wifi = WiFi.status() == WL_CONNECTED;
  if (wifi) {
    next.flags |= MF35X_NET_WIFI_CONNECTED;
    next.wifiRssi = (int16_t)WiFi.RSSI();
    if (!oldWifi && old.updatedMillis != 0) next.wifiReconnects++;
  }

  if (wifi && mf35xGatewayProbe(next.gatewayLatencyMs)) next.flags |= MF35X_NET_GATEWAY_OK;
  if (wifi && mf35xDnsProbe(next.dnsLatencyMs)) next.flags |= MF35X_NET_DNS_OK;
  if (wifi && mf35xFirebaseProbe(next.firebaseLatencyMs, next.firebaseHttpCode)) {
    next.flags |= MF35X_NET_FIREBASE_OK;
  }

  const bool internetOk =
    (next.flags & MF35X_NET_WIFI_CONNECTED) &&
    (next.flags & MF35X_NET_GATEWAY_OK) &&
    (next.flags & MF35X_NET_DNS_OK) &&
    (next.flags & MF35X_NET_FIREBASE_OK);
  if (internetOk) {
    next.consecutiveFailures = 0;
  } else {
    if (next.consecutiveFailures < 65535U) next.consecutiveFailures++;
    next.internetFailures++;
  }

  if (wifi) mf35xRutMetricsLesen(next);
  next.updatedMillis = millis();
  mf35xConnectivitySnapshotSchreiben(next);

  // Eigener Hintergrund-Task: beeinflusst die normale Live-loop nicht.
  if (wifi) {
    firebasePut("tracker/device/connectivity", mf35xConnectivityJson(next));
  }
}

uint32_t mf35xNetDiagCrc(const Mf35xNetDiagRecord& rec) {
  return offlineCrc32(
    reinterpret_cast<const uint8_t*>(&rec),
    offsetof(Mf35xNetDiagRecord, crc32)
  );
}

bool mf35xNetDiagValid(const Mf35xNetDiagRecord& rec) {
  return rec.magic == MF35X_NET_MAGIC &&
         rec.version == MF35X_NET_VERSION &&
         rec.size == sizeof(Mf35xNetDiagRecord) &&
         rec.crc32 == mf35xNetDiagCrc(rec);
}

Mf35xNetDiagRecord mf35xConnectivityRaceRecordBauen(uint32_t captureMillis) {
  const Mf35xConnectivitySnapshot s = mf35xConnectivitySnapshotLesen();
  Mf35xNetDiagRecord rec = {};
  rec.magic = MF35X_NET_MAGIC;
  rec.version = MF35X_NET_VERSION;
  rec.size = sizeof(rec);
  rec.bootId = offlineBootId;
  rec.flags = s.flags;
  rec.wifiRssi = s.wifiRssi;
  rec.gatewayLatencyMs = s.gatewayLatencyMs;
  rec.dnsLatencyMs = s.dnsLatencyMs;
  rec.firebaseLatencyMs = s.firebaseLatencyMs;
  rec.firebaseHttpCode = s.firebaseHttpCode;
  rec.consecutiveFailures = s.consecutiveFailures;
  rec.wifiReconnects = s.wifiReconnects;
  rec.internetFailures = s.internetFailures;
  const uint32_t age = s.updatedMillis == 0 ? 0xFFFFFFFFUL : (uint32_t)(captureMillis - s.updatedMillis);
  rec.probeAgeMs = age > 65535UL ? 65535U : (uint16_t)age;
  rec.lteRssi = s.lteRssi;
  rec.lteRsrp = s.lteRsrp;
  rec.lteRsrqDeci = s.lteRsrqDeci;
  rec.lteSinrDeci = s.lteSinrDeci;
  memcpy(rec.operatorName, s.operatorName, sizeof(rec.operatorName));
  memcpy(rec.networkType, s.networkType, sizeof(rec.networkType));
  rec.state = MF35X_NET_PENDING;
  rec.crc32 = mf35xNetDiagCrc(rec);
  return rec;
}

String mf35xNetDiagQueuePath(const String& raceId) {
  return String("/nq_") + raceId + ".bin";
}

String mf35xNetDiagRaceIdFromPath(String path) {
  if (!path.startsWith("/")) path = "/" + path;
  if (!path.startsWith("/nq_") || !path.endsWith(".bin")) return "";
  return path.substring(4, path.length() - 4);
}

String mf35xNetDiagSampleId(uint32_t bootId, uint32_t sequence) {
  char id[32];
  snprintf(id, sizeof(id), "b%08lx_s%08lx", (unsigned long)bootId, (unsigned long)sequence);
  return String(id);
}

String mf35xNetDiagJson(const Mf35xNetDiagRecord& rec) {
  String json = "{";
  json.reserve(900);
  bool first = true;
  jsonBoolFeld(json, first, "net_wifi_connected", (rec.flags & MF35X_NET_WIFI_CONNECTED) != 0);
  jsonLongFeld(json, first, "net_wifi_rssi_dbm", rec.wifiRssi);
  jsonBoolFeld(json, first, "net_gateway_reachable", (rec.flags & MF35X_NET_GATEWAY_OK) != 0);
  jsonULongFeld(json, first, "net_gateway_latency_ms", rec.gatewayLatencyMs);
  jsonBoolFeld(json, first, "net_dns_ok", (rec.flags & MF35X_NET_DNS_OK) != 0);
  jsonULongFeld(json, first, "net_dns_latency_ms", rec.dnsLatencyMs);
  jsonBoolFeld(json, first, "net_firebase_ok", (rec.flags & MF35X_NET_FIREBASE_OK) != 0);
  jsonLongFeld(json, first, "net_firebase_http_code", rec.firebaseHttpCode);
  jsonULongFeld(json, first, "net_firebase_latency_ms", rec.firebaseLatencyMs);
  jsonULongFeld(json, first, "net_failures_consecutive", rec.consecutiveFailures);
  jsonULongFeld(json, first, "net_wifi_reconnects", rec.wifiReconnects);
  jsonULongFeld(json, first, "net_internet_failures", rec.internetFailures);
  jsonULongFeld(json, first, "net_probe_age_ms", rec.probeAgeMs);
  jsonBoolFeld(json, first, "lte_api_configured", (rec.flags & MF35X_NET_RUT_CONFIGURED) != 0);
  jsonBoolFeld(json, first, "lte_api_ok", (rec.flags & MF35X_NET_RUT_API_OK) != 0);

  if (rec.flags & MF35X_NET_LTE_RSSI_VALID) jsonLongFeld(json, first, "lte_rssi_dbm", rec.lteRssi);
  else jsonRaw(json, first, "lte_rssi_dbm", "null");
  if (rec.flags & MF35X_NET_LTE_RSRP_VALID) jsonLongFeld(json, first, "lte_rsrp_dbm", rec.lteRsrp);
  else jsonRaw(json, first, "lte_rsrp_dbm", "null");
  if (rec.flags & MF35X_NET_LTE_RSRQ_VALID) jsonFloatFeld(json, first, "lte_rsrq_db", (double)rec.lteRsrqDeci / 10.0, 1);
  else jsonRaw(json, first, "lte_rsrq_db", "null");
  if (rec.flags & MF35X_NET_LTE_SINR_VALID) jsonFloatFeld(json, first, "lte_sinr_db", (double)rec.lteSinrDeci / 10.0, 1);
  else jsonRaw(json, first, "lte_sinr_db", "null");
  jsonText(json, first, "lte_operator", String(rec.operatorName));
  jsonText(json, first, "lte_network_type", String(rec.networkType));
  json += '}';
  return json;
}

bool mf35xNetDiagPatchSample(const String& raceId, const Mf35xNetDiagRecord& rec) {
  if (WiFi.status() != WL_CONNECTED || !offlineRaceIdGueltig(raceId)) return false;
  const String path = "tracker/races/" + raceId + "/samples/" +
                      mf35xNetDiagSampleId(rec.bootId, rec.sequence);
  String existing;
  if (!firebaseGet(path, existing) || existing == "null" || existing.indexOf("\"timestamp\"") < 0) {
    return false;
  }
  return firebasePatch(path, mf35xNetDiagJson(rec), false);
}

bool mf35xNetDiagQueueAppend(const String& raceId, const Mf35xNetDiagRecord& rec) {
  if (!offlineBufferReady || !offlineRaceIdGueltig(raceId)) return false;
  offlineFsStatusAktualisieren();
  const size_t freeBytes = offlineFsTotalBytes > offlineFsUsedBytes
    ? offlineFsTotalBytes - offlineFsUsedBytes : 0;
  if (freeBytes <= MF35X_NET_FLASH_PROTECT_BYTES + sizeof(rec)) {
    mf35xNetDiagDropped++;
    return false;
  }

  File f = LittleFS.open(mf35xNetDiagQueuePath(raceId), FILE_APPEND);
  if (!f) {
    mf35xNetDiagDropped++;
    return false;
  }
  const size_t written = f.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(rec));
  f.flush();
  f.close();
  if (written != sizeof(rec)) {
    mf35xNetDiagDropped++;
    return false;
  }
  mf35xNetDiagQueued++;
  return true;
}

bool mf35xNetDiagFileHasPending(const String& path) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  Mf35xNetDiagRecord rec;
  while (f.available() >= (int)sizeof(rec)) {
    if (f.read(reinterpret_cast<uint8_t*>(&rec), sizeof(rec)) != sizeof(rec)) break;
    if (mf35xNetDiagValid(rec) && rec.state != MF35X_NET_SENT) {
      f.close();
      return true;
    }
  }
  f.close();
  return false;
}

bool mf35xNetDiagMarkSent(File& f, size_t offset) {
  if (!f.seek(offset + offsetof(Mf35xNetDiagRecord, state), SeekSet)) return false;
  const uint8_t sent = MF35X_NET_SENT;
  if (f.write(&sent, 1) != 1) return false;
  f.flush();
  return true;
}

void mf35xNetDiagDrainOne() {
  if (!offlineBufferReady || WiFi.status() != WL_CONNECTED) return;
  const unsigned long now = millis();
  if ((unsigned long)(now - mf35xNetLastDrainMs) < MF35X_NET_DRAIN_MS) return;
  mf35xNetLastDrainMs = now;

  File root = LittleFS.open("/");
  if (!root) return;
  String path = "";
  File e = root.openNextFile();
  while (e) {
    String name = e.name();
    e.close();
    if (!name.startsWith("/")) name = "/" + name;
    if (mf35xNetDiagRaceIdFromPath(name).length() > 0 && mf35xNetDiagFileHasPending(name)) {
      path = name;
      break;
    }
    e = root.openNextFile();
  }
  root.close();
  if (path.length() == 0) return;

  const String raceId = mf35xNetDiagRaceIdFromPath(path);
  File f = LittleFS.open(path, "r+");
  if (!f) return;
  size_t offset = 0;
  Mf35xNetDiagRecord rec;
  while (f.available() >= (int)sizeof(rec)) {
    if (f.read(reinterpret_cast<uint8_t*>(&rec), sizeof(rec)) != sizeof(rec)) break;
    if (mf35xNetDiagValid(rec) && rec.state != MF35X_NET_SENT) {
      if (mf35xNetDiagPatchSample(raceId, rec) && mf35xNetDiagMarkSent(f, offset)) {
        mf35xNetDiagReplayed++;
      }
      break;
    }
    offset += sizeof(rec);
  }
  f.close();
}

void mf35xConnectivityTask(void*) {
  unsigned long lastProbe = 0;
  for (;;) {
    const unsigned long now = millis();
    if (lastProbe == 0 || (unsigned long)(now - lastProbe) >= MF35X_NET_PROBE_INTERVAL_MS) {
      lastProbe = now;
      mf35xConnectivityProbe();
    }
    vTaskDelay(MF35X_NET_TASK_IDLE);
  }
}

void mf35xConnectivityDiagnosticsSetup() {
  if (mf35xConnectivityTaskHandle) return;
  Mf35xConnectivitySnapshot initial = {};
  initial.wifiRssi = -127;
  initial.updatedMillis = 0;
  mf35xConnectivitySnapshotSchreiben(initial);

  const BaseType_t ok = xTaskCreatePinnedToCore(
    mf35xConnectivityTask,
    "mf35x_net_diag",
    MF35X_NET_TASK_STACK,
    nullptr,
    MF35X_NET_TASK_PRIORITY,
    &mf35xConnectivityTaskHandle,
    MF35X_NET_TASK_CORE
  );
  if (ok != pdPASS) {
    mf35xConnectivityTaskHandle = nullptr;
    Serial.println("NET-DIAG: FEHLER - Task konnte nicht gestartet werden");
    return;
  }
  Serial.println("NET-DIAG: WLAN/Gateway/DNS/Firebase + optionale RUT200-LTE-Diagnose aktiv");
}
