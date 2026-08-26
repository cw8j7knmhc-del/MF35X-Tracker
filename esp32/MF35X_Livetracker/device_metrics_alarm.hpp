#pragma once

#include <LittleFS.h>
#include <FS.h>
#include <stddef.h>

// ==================================================
// V5.9.13 - ESP32 ALS QUELLE FUER MAXIMALWERTE + ALARMHISTORIE
// ==================================================
// - Besucherbrowser lesen diese Daten nur noch.
// - Maximalwerte werden lokal ermittelt, in NVS gesichert und nach Firebase gespiegelt.
// - Oeldruck-Minimum und Oeldruckalarm gelten erst ab 400 U/min + 5 s Startverzoegerung.
// - Alarmereignisse werden zuerst dauerhaft in LittleFS gepuffert.
// - 30 feste Firebase-Slots verhindern unendliches Wachstum und machen Wiederholungen idempotent.
// ==================================================

constexpr uint16_t DEVICE_MAX_SPEED_VALID = 1u << 0;
constexpr uint16_t DEVICE_MAX_RPM_VALID = 1u << 1;
constexpr uint16_t DEVICE_MAX_OIL_TEMP_VALID = 1u << 2;
constexpr uint16_t DEVICE_MAX_CYL_TEMP_VALID = 1u << 3;
constexpr uint16_t DEVICE_MIN_OIL_PRESSURE_VALID = 1u << 4;
constexpr uint16_t DEVICE_MIN_BATTERY_VALID = 1u << 5;

constexpr unsigned long DEVICE_MAX_NVS_INTERVAL_MS = 60000UL;
constexpr unsigned long DEVICE_MAX_UPLOAD_RETRY_MS = 3000UL;
constexpr unsigned long DEVICE_MAX_REMOTE_MERGE_RETRY_MS = 5000UL;
constexpr float DEVICE_OIL_PRESSURE_RPM_MIN = 400.0f;
constexpr unsigned long DEVICE_OIL_PRESSURE_START_DELAY_MS = 5000UL;

struct DeviceMaxValues {
  float maxSpeed = NAN;
  float maxRpm = NAN;
  float maxOilTemp = NAN;
  float maxCylTemp = NAN;
  float minOilPressure = NAN;
  float minBattery = NAN;
  uint16_t validMask = 0;
};

DeviceMaxValues deviceMaxValues;
bool deviceMaxDirty = false;
bool deviceMaxResetTimestampPending = false;
bool deviceMaxFirebaseMerged = false;
unsigned long deviceMaxLastPersistMs = 0;
unsigned long deviceMaxLastUploadAttemptMs = 0;
unsigned long deviceMaxLastMergeAttemptMs = 0;
unsigned long deviceOilPressureEngineStartAt = 0;

constexpr uint32_t DEVICE_ALARM_MAGIC = 0x4D464131UL; // "MFA1"
constexpr uint16_t DEVICE_ALARM_RECORD_VERSION = 1;
constexpr uint8_t DEVICE_ALARM_PENDING = 0xA5;
constexpr uint8_t DEVICE_ALARM_SENT = 0x5A;
constexpr size_t DEVICE_ALARM_FLASH_EXTRA_RESERVE = 8192UL;
constexpr unsigned long DEVICE_ALARM_DRAIN_OK_INTERVAL_MS = 300UL;
constexpr unsigned long DEVICE_ALARM_DRAIN_ERROR_BACKOFF_MS = 3000UL;
constexpr uint8_t DEVICE_ALARM_HISTORY_SLOTS = 30;

constexpr uint16_t DEVICE_ALARM_BAT_WARN = 1u << 0;
constexpr uint16_t DEVICE_ALARM_BAT_ALARM = 1u << 1;
constexpr uint16_t DEVICE_ALARM_OILP_WARN = 1u << 2;
constexpr uint16_t DEVICE_ALARM_OILP_ALARM = 1u << 3;
constexpr uint16_t DEVICE_ALARM_OILT_WARN = 1u << 4;
constexpr uint16_t DEVICE_ALARM_OILT_ALARM = 1u << 5;
constexpr uint16_t DEVICE_ALARM_CYL_WARN = 1u << 6;
constexpr uint16_t DEVICE_ALARM_CYL_ALARM = 1u << 7;

constexpr uint8_t DEVICE_ALARM_KIND_BAT_WARN = 0;
constexpr uint8_t DEVICE_ALARM_KIND_BAT_ALARM = 1;
constexpr uint8_t DEVICE_ALARM_KIND_OILP_WARN = 2;
constexpr uint8_t DEVICE_ALARM_KIND_OILP_ALARM = 3;
constexpr uint8_t DEVICE_ALARM_KIND_OILT_WARN = 4;
constexpr uint8_t DEVICE_ALARM_KIND_OILT_ALARM = 5;
constexpr uint8_t DEVICE_ALARM_KIND_CYL_WARN = 6;
constexpr uint8_t DEVICE_ALARM_KIND_CYL_ALARM = 7;

#pragma pack(push, 1)
struct DeviceAlarmRecord {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t bootId;
  uint32_t sequence;
  uint32_t capturedMillis;
  uint64_t capturedEpochMs;
  int32_t valueCenti;
  uint8_t kind;
  uint8_t reserved[3];
  uint32_t crc32;
  uint8_t state;
  uint8_t padding[7];
};
#pragma pack(pop)

static_assert(sizeof(DeviceAlarmRecord) == 48, "DeviceAlarmRecord muss exakt 48 Byte gross sein");

const char* DEVICE_ALARM_QUEUE_PATH = "/alarmq.bin";
const char* DEVICE_ALARM_QUEUE_REPAIR_PATH = "/alarmq.repair";

bool deviceAlarmQueueReady = false;
uint32_t deviceAlarmSequence = 0;
uint32_t deviceAlarmPendingCount = 0;
uint32_t deviceAlarmQueuedCount = 0;
uint32_t deviceAlarmReplayedCount = 0;
uint32_t deviceAlarmDroppedCount = 0;
uint32_t deviceAlarmCorruptCount = 0;
uint16_t deviceAlarmActiveMask = 0;
unsigned long deviceAlarmLastDrainAttemptMs = 0;
String deviceAlarmLastError = "";
String deviceLastMaxResetCommandId = "";

bool deviceFloatBesserMax(float candidate, float current, bool valid) {
  return isfinite(candidate) && (!valid || candidate > current);
}

bool deviceFloatBesserMin(float candidate, float current, bool valid) {
  return isfinite(candidate) && (!valid || candidate < current);
}

void deviceMaxLaden() {
  if (!preferencesOk) return;

  deviceMaxValues.validMask = (uint16_t)preferences.getUInt("mx_mask", 0U);
  deviceMaxValues.maxSpeed = preferences.getFloat("mx_spd", NAN);
  deviceMaxValues.maxRpm = preferences.getFloat("mx_rpm", NAN);
  deviceMaxValues.maxOilTemp = preferences.getFloat("mx_ot", NAN);
  deviceMaxValues.maxCylTemp = preferences.getFloat("mx_ct", NAN);
  deviceMaxValues.minOilPressure = preferences.getFloat("mn_op", NAN);
  deviceMaxValues.minBattery = preferences.getFloat("mn_bat", NAN);

  deviceAlarmSequence = preferences.getULong("alarm_seq", 0UL);
  deviceLastMaxResetCommandId = preferences.getString("cmd_max", "");
}

void deviceMaxSpeichern(bool force = false) {
  if (!preferencesOk || !deviceMaxDirty) return;

  const unsigned long now = millis();
  if (!force && (unsigned long)(now - deviceMaxLastPersistMs) < DEVICE_MAX_NVS_INTERVAL_MS) {
    return;
  }

  preferences.putUInt("mx_mask", deviceMaxValues.validMask);
  preferences.putFloat("mx_spd", deviceMaxValues.maxSpeed);
  preferences.putFloat("mx_rpm", deviceMaxValues.maxRpm);
  preferences.putFloat("mx_ot", deviceMaxValues.maxOilTemp);
  preferences.putFloat("mx_ct", deviceMaxValues.maxCylTemp);
  preferences.putFloat("mn_op", deviceMaxValues.minOilPressure);
  preferences.putFloat("mn_bat", deviceMaxValues.minBattery);
  deviceMaxLastPersistMs = now;
}

void deviceMaxWertSetzen(uint16_t bit, float& target, float value, bool maximum) {
  const bool valid = (deviceMaxValues.validMask & bit) != 0;
  const bool besser = maximum
    ? deviceFloatBesserMax(value, target, valid)
    : deviceFloatBesserMin(value, target, valid);

  if (!besser) return;
  target = value;
  deviceMaxValues.validMask |= bit;
  deviceMaxDirty = true;
}

void deviceEngineStateAktualisieren() {
  const bool running = rpmSignalOk && isfinite(rpm) && rpm >= DEVICE_OIL_PRESSURE_RPM_MIN;
  if (!running) {
    deviceOilPressureEngineStartAt = 0;
    return;
  }

  if (deviceOilPressureEngineStartAt == 0) {
    deviceOilPressureEngineStartAt = millis();
  }
}

bool deviceOilPressureFreigegeben() {
  if (deviceOilPressureEngineStartAt == 0) return false;
  return (unsigned long)(millis() - deviceOilPressureEngineStartAt) >=
         DEVICE_OIL_PRESSURE_START_DELAY_MS;
}

void deviceMaxAktualisieren() {
  deviceEngineStateAktualisieren();

  const GpsSnapshot gpsData = gpsSnapshotLesen();
  if (gpsFixAktuell(gpsData) && gpsData.speedValid) {
    deviceMaxWertSetzen(
      DEVICE_MAX_SPEED_VALID,
      deviceMaxValues.maxSpeed,
      (float)gpsData.speedKmh,
      true
    );
  }

  if (rpmSignalOk && isfinite(rpm)) {
    deviceMaxWertSetzen(
      DEVICE_MAX_RPM_VALID,
      deviceMaxValues.maxRpm,
      rpm,
      true
    );
  }

  if (isfinite(oilTemp)) {
    deviceMaxWertSetzen(
      DEVICE_MAX_OIL_TEMP_VALID,
      deviceMaxValues.maxOilTemp,
      oilTemp,
      true
    );
  }

  if (isfinite(cylinderTemp)) {
    deviceMaxWertSetzen(
      DEVICE_MAX_CYL_TEMP_VALID,
      deviceMaxValues.maxCylTemp,
      (float)cylinderTemp,
      true
    );
  }

  if (deviceOilPressureFreigegeben() && isfinite(oilPressureBar)) {
    deviceMaxWertSetzen(
      DEVICE_MIN_OIL_PRESSURE_VALID,
      deviceMaxValues.minOilPressure,
      oilPressureBar,
      false
    );
  }

  if (isfinite(batteryVoltage)) {
    deviceMaxWertSetzen(
      DEVICE_MIN_BATTERY_VALID,
      deviceMaxValues.minBattery,
      batteryVoltage,
      false
    );
  }
}

void deviceMaxMergeWert(const String& json, const char* key, uint16_t bit, float& target, bool maximum) {
  double v = 0.0;
  if (!jsonZahl(json, key, v) || !isfinite(v)) return;
  deviceMaxWertSetzen(bit, target, (float)v, maximum);
}

void deviceMaxFirebaseMergeBearbeiten() {
  if (deviceMaxFirebaseMerged || WiFi.status() != WL_CONNECTED) return;

  const unsigned long now = millis();
  if ((unsigned long)(now - deviceMaxLastMergeAttemptMs) < DEVICE_MAX_REMOTE_MERGE_RETRY_MS) return;
  deviceMaxLastMergeAttemptMs = now;

  String json;
  if (!firebaseGet("tracker/maxValues", json)) return;

  deviceMaxMergeWert(json, "maxSpeed", DEVICE_MAX_SPEED_VALID, deviceMaxValues.maxSpeed, true);
  deviceMaxMergeWert(json, "maxRpm", DEVICE_MAX_RPM_VALID, deviceMaxValues.maxRpm, true);
  deviceMaxMergeWert(json, "maxOilTemp", DEVICE_MAX_OIL_TEMP_VALID, deviceMaxValues.maxOilTemp, true);
  deviceMaxMergeWert(json, "maxCylTemp", DEVICE_MAX_CYL_TEMP_VALID, deviceMaxValues.maxCylTemp, true);
  deviceMaxMergeWert(json, "minOilPressure", DEVICE_MIN_OIL_PRESSURE_VALID, deviceMaxValues.minOilPressure, false);
  deviceMaxMergeWert(json, "minBattery", DEVICE_MIN_BATTERY_VALID, deviceMaxValues.minBattery, false);

  deviceMaxFirebaseMerged = true;
  deviceMaxDirty = true;
}

void deviceJsonOptionalFloat(
  String& json,
  bool& first,
  const char* key,
  uint16_t bit,
  float value,
  int decimals
) {
  if ((deviceMaxValues.validMask & bit) != 0 && isfinite(value)) {
    jsonFloatFeld(json, first, key, value, decimals);
  } else {
    jsonRaw(json, first, key, "null");
  }
}

String deviceMaxJsonBauen() {
  String json = "{";
  json.reserve(320);
  bool first = true;

  deviceJsonOptionalFloat(json, first, "maxSpeed", DEVICE_MAX_SPEED_VALID, deviceMaxValues.maxSpeed, 2);
  deviceJsonOptionalFloat(json, first, "maxRpm", DEVICE_MAX_RPM_VALID, deviceMaxValues.maxRpm, 0);
  deviceJsonOptionalFloat(json, first, "maxOilTemp", DEVICE_MAX_OIL_TEMP_VALID, deviceMaxValues.maxOilTemp, 1);
  deviceJsonOptionalFloat(json, first, "maxCylTemp", DEVICE_MAX_CYL_TEMP_VALID, deviceMaxValues.maxCylTemp, 1);
  deviceJsonOptionalFloat(json, first, "minOilPressure", DEVICE_MIN_OIL_PRESSURE_VALID, deviceMaxValues.minOilPressure, 2);
  deviceJsonOptionalFloat(json, first, "minBattery", DEVICE_MIN_BATTERY_VALID, deviceMaxValues.minBattery, 2);
  jsonBoolFeld(json, first, "deviceOwned", true);
  if (deviceMaxResetTimestampPending) {
    jsonRaw(json, first, "resetAt", "{\".sv\":\"timestamp\"}");
  }

  json += '}';
  return json;
}

void deviceMaxUploadBearbeiten() {
  if (!deviceMaxDirty || !deviceMaxFirebaseMerged || WiFi.status() != WL_CONNECTED) return;

  const unsigned long now = millis();
  if ((unsigned long)(now - deviceMaxLastUploadAttemptMs) < DEVICE_MAX_UPLOAD_RETRY_MS) return;
  deviceMaxLastUploadAttemptMs = now;

  if (firebasePatch("tracker/maxValues", deviceMaxJsonBauen(), false)) {
    deviceMaxDirty = false;
    deviceMaxResetTimestampPending = false;
    deviceMaxSpeichern(true);
  }
}

void deviceMaxReset() {
  deviceMaxValues = DeviceMaxValues{};
  deviceMaxDirty = true;
  deviceMaxResetTimestampPending = true;
  deviceMaxFirebaseMerged = true;
  deviceOilPressureEngineStartAt = 0;
  deviceMaxAktualisieren();
  deviceMaxSpeichern(true);
  deviceMaxUploadBearbeiten();
}

uint32_t deviceAlarmCrc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
    }
  }
  return ~crc;
}

uint32_t deviceAlarmRecordCrc(const DeviceAlarmRecord& rec) {
  return deviceAlarmCrc32(
    reinterpret_cast<const uint8_t*>(&rec),
    offsetof(DeviceAlarmRecord, crc32)
  );
}

bool deviceAlarmRecordGueltig(const DeviceAlarmRecord& rec) {
  return rec.magic == DEVICE_ALARM_MAGIC &&
         rec.version == DEVICE_ALARM_RECORD_VERSION &&
         rec.size == sizeof(DeviceAlarmRecord) &&
         rec.kind <= DEVICE_ALARM_KIND_CYL_ALARM &&
         rec.crc32 == deviceAlarmRecordCrc(rec);
}

void deviceAlarmQueueReparieren() {
  if (!deviceAlarmQueueReady || !LittleFS.exists(DEVICE_ALARM_QUEUE_PATH)) return;

  File src = LittleFS.open(DEVICE_ALARM_QUEUE_PATH, "r");
  if (!src) return;
  const size_t originalSize = src.size();
  src.close();

  const size_t validSize = originalSize - (originalSize % sizeof(DeviceAlarmRecord));
  if (validSize == originalSize) return;

  LittleFS.remove(DEVICE_ALARM_QUEUE_REPAIR_PATH);
  src = LittleFS.open(DEVICE_ALARM_QUEUE_PATH, "r");
  File dst = LittleFS.open(DEVICE_ALARM_QUEUE_REPAIR_PATH, "w");
  if (!src || !dst) {
    if (src) src.close();
    if (dst) dst.close();
    LittleFS.remove(DEVICE_ALARM_QUEUE_REPAIR_PATH);
    deviceAlarmLastError = "Alarmqueue-Reparatur konnte nicht gestartet werden";
    return;
  }

  uint8_t buf[192];
  size_t remaining = validSize;
  bool ok = true;
  while (remaining > 0) {
    const size_t n = remaining > sizeof(buf) ? sizeof(buf) : remaining;
    if (src.read(buf, n) != n || dst.write(buf, n) != n) {
      ok = false;
      break;
    }
    remaining -= n;
  }

  dst.flush();
  src.close();
  dst.close();

  if (!ok || remaining != 0 || !LittleFS.remove(DEVICE_ALARM_QUEUE_PATH) ||
      !LittleFS.rename(DEVICE_ALARM_QUEUE_REPAIR_PATH, DEVICE_ALARM_QUEUE_PATH)) {
    LittleFS.remove(DEVICE_ALARM_QUEUE_REPAIR_PATH);
    deviceAlarmLastError = "Alarmqueue-Reparatur fehlgeschlagen";
    return;
  }

  deviceAlarmCorruptCount++;
}

void deviceAlarmQueueScannen() {
  deviceAlarmPendingCount = 0;
  if (!deviceAlarmQueueReady || !LittleFS.exists(DEVICE_ALARM_QUEUE_PATH)) return;

  File f = LittleFS.open(DEVICE_ALARM_QUEUE_PATH, "r");
  if (!f) {
    deviceAlarmLastError = "Alarmqueue konnte nicht gelesen werden";
    return;
  }

  DeviceAlarmRecord rec;
  while (f.available() >= (int)sizeof(rec)) {
    if (f.read(reinterpret_cast<uint8_t*>(&rec), sizeof(rec)) != sizeof(rec)) break;
    if (!deviceAlarmRecordGueltig(rec)) {
      deviceAlarmCorruptCount++;
      continue;
    }
    if (rec.state != DEVICE_ALARM_SENT) deviceAlarmPendingCount++;
  }
  f.close();

  if (deviceAlarmPendingCount == 0) {
    LittleFS.remove(DEVICE_ALARM_QUEUE_PATH);
  }
  offlineFsStatusAktualisieren();
}

void deviceAlarmQueueInitialisieren() {
  deviceAlarmQueueReady = offlineBufferReady;
  if (!deviceAlarmQueueReady) {
    deviceAlarmLastError = "LittleFS nicht bereit - Alarmqueue nur Online-Fallback";
    return;
  }

  deviceAlarmQueueReparieren();
  deviceAlarmQueueScannen();
}

bool deviceAlarmQueueAppend(const DeviceAlarmRecord& rec) {
  if (!deviceAlarmQueueReady) return false;

  offlineFsStatusAktualisieren();
  const size_t freeBytes = offlineFsTotalBytes > offlineFsUsedBytes
    ? offlineFsTotalBytes - offlineFsUsedBytes
    : 0;

  if (freeBytes <= OFFLINE_FLASH_RESERVE_BYTES + DEVICE_ALARM_FLASH_EXTRA_RESERVE + sizeof(rec)) {
    deviceAlarmLastError = "Zu wenig Flash fuer Alarmqueue";
    return false;
  }

  File f = LittleFS.open(DEVICE_ALARM_QUEUE_PATH, "a");
  if (!f) {
    deviceAlarmLastError = "Alarmqueue konnte nicht geoeffnet werden";
    return false;
  }

  const size_t written = f.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(rec));
  f.flush();
  f.close();

  if (written != sizeof(rec)) {
    deviceAlarmLastError = "Alarmqueue-Schreibfehler";
    return false;
  }

  deviceAlarmPendingCount++;
  deviceAlarmQueuedCount++;
  offlineFsStatusAktualisieren();
  return true;
}

bool deviceAlarmNaechstenPendingLesen(DeviceAlarmRecord& out, size_t& offsetOut) {
  if (!deviceAlarmQueueReady || !LittleFS.exists(DEVICE_ALARM_QUEUE_PATH)) return false;

  File f = LittleFS.open(DEVICE_ALARM_QUEUE_PATH, "r");
  if (!f) return false;

  size_t offset = 0;
  DeviceAlarmRecord rec;
  while (f.available() >= (int)sizeof(rec)) {
    if (f.read(reinterpret_cast<uint8_t*>(&rec), sizeof(rec)) != sizeof(rec)) break;
    if (deviceAlarmRecordGueltig(rec) && rec.state != DEVICE_ALARM_SENT) {
      out = rec;
      offsetOut = offset;
      f.close();
      return true;
    }
    offset += sizeof(rec);
  }

  f.close();
  return false;
}

bool deviceAlarmAlsGesendetMarkieren(size_t offset) {
  File f = LittleFS.open(DEVICE_ALARM_QUEUE_PATH, "r+");
  if (!f) return false;
  if (!f.seek(offset + offsetof(DeviceAlarmRecord, state), SeekSet)) {
    f.close();
    return false;
  }
  const uint8_t sent = DEVICE_ALARM_SENT;
  const bool ok = f.write(&sent, 1) == 1;
  f.flush();
  f.close();
  return ok;
}

const char* deviceAlarmKey(uint8_t kind) {
  switch (kind) {
    case DEVICE_ALARM_KIND_BAT_WARN: return "battery_warning";
    case DEVICE_ALARM_KIND_BAT_ALARM: return "battery_alarm";
    case DEVICE_ALARM_KIND_OILP_WARN: return "oilPressure_warning";
    case DEVICE_ALARM_KIND_OILP_ALARM: return "oilPressure_alarm";
    case DEVICE_ALARM_KIND_OILT_WARN: return "oilTemp_warning";
    case DEVICE_ALARM_KIND_OILT_ALARM: return "oilTemp_alarm";
    case DEVICE_ALARM_KIND_CYL_WARN: return "cylTemp_warning";
    case DEVICE_ALARM_KIND_CYL_ALARM: return "cylTemp_alarm";
    default: return "unknown";
  }
}

const char* deviceAlarmLevel(uint8_t kind) {
  return (kind % 2) == 0 ? "warning" : "alarm";
}

const char* deviceAlarmUnit(uint8_t kind) {
  if (kind <= DEVICE_ALARM_KIND_BAT_ALARM) return "V";
  if (kind <= DEVICE_ALARM_KIND_OILP_ALARM) return "bar";
  return "°C";
}

const char* deviceAlarmLabel(uint8_t kind) {
  if (kind <= DEVICE_ALARM_KIND_BAT_ALARM) return "Batteriespannung";
  if (kind <= DEVICE_ALARM_KIND_OILP_ALARM) return "Öldruck";
  if (kind <= DEVICE_ALARM_KIND_OILT_ALARM) return "Öltemperatur";
  return "Zylindertemperatur";
}

String deviceAlarmText(const DeviceAlarmRecord& rec) {
  const float value = (float)rec.valueCenti / 100.0f;
  const bool alarm = strcmp(deviceAlarmLevel(rec.kind), "alarm") == 0;
  String text = deviceAlarmLabel(rec.kind);
  text += alarm ? " kritisch: " : " Warnung: ";
  const int decimals = rec.kind <= DEVICE_ALARM_KIND_OILP_ALARM ? 2 : 1;
  text += String(value, decimals);
  text += ' ';
  text += deviceAlarmUnit(rec.kind);
  return text;
}

String deviceAlarmEventId(const DeviceAlarmRecord& rec) {
  char id[24];
  snprintf(id, sizeof(id), "a%08lx", (unsigned long)rec.sequence);
  return String(id);
}

String deviceAlarmFirebasePath(const DeviceAlarmRecord& rec) {
  char slot[8];
  snprintf(slot, sizeof(slot), "s%02u", (unsigned)(rec.sequence % DEVICE_ALARM_HISTORY_SLOTS));
  return String("tracker/alarmHistory/") + slot;
}

String deviceAlarmJson(const DeviceAlarmRecord& rec) {
  String json = "{";
  json.reserve(500);
  bool first = true;

  jsonText(json, first, "id", deviceAlarmEventId(rec));
  jsonULongFeld(json, first, "sequence", rec.sequence);
  jsonText(json, first, "key", deviceAlarmKey(rec.kind));
  jsonText(json, first, "level", deviceAlarmLevel(rec.kind));
  jsonText(json, first, "text", deviceAlarmText(rec));
  jsonFloatFeld(json, first, "value", (double)rec.valueCenti / 100.0, 2);
  jsonText(json, first, "unit", deviceAlarmUnit(rec.kind));
  jsonULongFeld(json, first, "capturedUptimeMs", rec.capturedMillis);

  if (rec.capturedEpochMs > 1700000000000ULL) {
    jsonRaw(json, first, "timestamp", offlineUInt64String(rec.capturedEpochMs));
    jsonText(json, first, "timeSource", "gps_utc");
  } else {
    jsonRaw(json, first, "timestamp", "null");
    jsonText(json, first, "timeSource", "uptime");
  }

  jsonRaw(json, first, "uploadedAt", "{\".sv\":\"timestamp\"}");
  jsonBoolFeld(json, first, "deviceOwned", true);
  json += '}';
  return json;
}

DeviceAlarmRecord deviceAlarmRecordBauen(uint8_t kind, float value) {
  DeviceAlarmRecord rec = {};
  rec.magic = DEVICE_ALARM_MAGIC;
  rec.version = DEVICE_ALARM_RECORD_VERSION;
  rec.size = sizeof(DeviceAlarmRecord);
  rec.bootId = offlineBootId;
  rec.sequence = ++deviceAlarmSequence;
  rec.capturedMillis = millis();
  rec.valueCenti = (int32_t)lroundf(value * 100.0f);
  rec.kind = kind;
  rec.state = DEVICE_ALARM_PENDING;

  const GpsSnapshot gpsData = gpsSnapshotLesen();
  if (gpsData.utcValid && gpsData.utcEpochMs > 1700000000000ULL) {
    const unsigned long delta = (unsigned long)(rec.capturedMillis - gpsData.utcUpdateMillis);
    if (delta <= 10000UL) {
      rec.capturedEpochMs = gpsData.utcEpochMs + (uint64_t)delta;
    }
  }

  rec.crc32 = deviceAlarmRecordCrc(rec);

  if (preferencesOk) {
    preferences.putULong("alarm_seq", deviceAlarmSequence);
  }
  return rec;
}

void deviceAlarmEreignis(uint8_t kind, float value) {
  if (!isfinite(value)) return;
  const DeviceAlarmRecord rec = deviceAlarmRecordBauen(kind, value);

  if (deviceAlarmQueueAppend(rec)) return;

  if (WiFi.status() == WL_CONNECTED &&
      firebasePut(deviceAlarmFirebasePath(rec), deviceAlarmJson(rec))) {
    deviceAlarmReplayedCount++;
    return;
  }

  deviceAlarmDroppedCount++;
}

void deviceAlarmBitSetzen(
  uint16_t& mask,
  uint16_t warningBit,
  uint16_t alarmBit,
  float value,
  bool low,
  float warn,
  float alarm
) {
  if (!isfinite(value)) return;

  const bool isAlarm = low ? value <= alarm : value >= alarm;
  const bool isWarn = !isAlarm && (low ? value <= warn : value >= warn);
  if (isAlarm) mask |= alarmBit;
  else if (isWarn) mask |= warningBit;
}

void deviceAlarmAktualisieren() {
  deviceEngineStateAktualisieren();

  uint16_t current = 0;
  deviceAlarmBitSetzen(
    current,
    DEVICE_ALARM_BAT_WARN,
    DEVICE_ALARM_BAT_ALARM,
    batteryVoltage,
    true,
    alarmConfig.batteryWarn,
    alarmConfig.batteryAlarm
  );

  if (deviceOilPressureFreigegeben()) {
    deviceAlarmBitSetzen(
      current,
      DEVICE_ALARM_OILP_WARN,
      DEVICE_ALARM_OILP_ALARM,
      oilPressureBar,
      true,
      alarmConfig.oilPressureWarn,
      alarmConfig.oilPressureAlarm
    );
  }

  deviceAlarmBitSetzen(
    current,
    DEVICE_ALARM_OILT_WARN,
    DEVICE_ALARM_OILT_ALARM,
    oilTemp,
    false,
    alarmConfig.oilTempWarn,
    alarmConfig.oilTempAlarm
  );

  deviceAlarmBitSetzen(
    current,
    DEVICE_ALARM_CYL_WARN,
    DEVICE_ALARM_CYL_ALARM,
    (float)cylinderTemp,
    false,
    alarmConfig.cylTempWarn,
    alarmConfig.cylTempAlarm
  );

  const uint16_t newlyActive = current & ~deviceAlarmActiveMask;
  deviceAlarmActiveMask = current;

  if (newlyActive & DEVICE_ALARM_BAT_WARN) deviceAlarmEreignis(DEVICE_ALARM_KIND_BAT_WARN, batteryVoltage);
  if (newlyActive & DEVICE_ALARM_BAT_ALARM) deviceAlarmEreignis(DEVICE_ALARM_KIND_BAT_ALARM, batteryVoltage);
  if (newlyActive & DEVICE_ALARM_OILP_WARN) deviceAlarmEreignis(DEVICE_ALARM_KIND_OILP_WARN, oilPressureBar);
  if (newlyActive & DEVICE_ALARM_OILP_ALARM) deviceAlarmEreignis(DEVICE_ALARM_KIND_OILP_ALARM, oilPressureBar);
  if (newlyActive & DEVICE_ALARM_OILT_WARN) deviceAlarmEreignis(DEVICE_ALARM_KIND_OILT_WARN, oilTemp);
  if (newlyActive & DEVICE_ALARM_OILT_ALARM) deviceAlarmEreignis(DEVICE_ALARM_KIND_OILT_ALARM, oilTemp);
  if (newlyActive & DEVICE_ALARM_CYL_WARN) deviceAlarmEreignis(DEVICE_ALARM_KIND_CYL_WARN, (float)cylinderTemp);
  if (newlyActive & DEVICE_ALARM_CYL_ALARM) deviceAlarmEreignis(DEVICE_ALARM_KIND_CYL_ALARM, (float)cylinderTemp);
}

void deviceAlarmDrainBearbeiten() {
  if (!deviceAlarmQueueReady || deviceAlarmPendingCount == 0 || WiFi.status() != WL_CONNECTED) return;

  const unsigned long now = millis();
  if ((unsigned long)(now - deviceAlarmLastDrainAttemptMs) < DEVICE_ALARM_DRAIN_OK_INTERVAL_MS) return;
  deviceAlarmLastDrainAttemptMs = now;

  DeviceAlarmRecord rec;
  size_t offset = 0;
  if (!deviceAlarmNaechstenPendingLesen(rec, offset)) {
    deviceAlarmQueueScannen();
    return;
  }

  if (!firebasePut(deviceAlarmFirebasePath(rec), deviceAlarmJson(rec))) {
    deviceAlarmLastDrainAttemptMs = now +
      (DEVICE_ALARM_DRAIN_ERROR_BACKOFF_MS - DEVICE_ALARM_DRAIN_OK_INTERVAL_MS);
    return;
  }

  if (!deviceAlarmAlsGesendetMarkieren(offset)) {
    deviceAlarmLastError = "Alarmqueue konnte nach Upload nicht bestaetigt werden";
    return;
  }

  if (deviceAlarmPendingCount > 0) deviceAlarmPendingCount--;
  deviceAlarmReplayedCount++;

  if (deviceAlarmPendingCount == 0) {
    LittleFS.remove(DEVICE_ALARM_QUEUE_PATH);
    offlineFsStatusAktualisieren();
  }
}

void deviceDerivedDataInitialisieren() {
  deviceMaxLaden();
  deviceAlarmQueueInitialisieren();
}

void deviceDerivedDataAktualisieren() {
  deviceMaxAktualisieren();
  deviceAlarmAktualisieren();
}

void deviceDerivedDataBearbeiten() {
  deviceMaxSpeichern(false);
  deviceMaxFirebaseMergeBearbeiten();
  deviceMaxUploadBearbeiten();
  deviceAlarmDrainBearbeiten();
}

void deviceDerivedDataCommandBearbeiten(const String& commandsJson) {
  String requestId;
  String status;

  if (!commandAusObjektLesen(commandsJson, "max_values_reset", requestId, status)) return;
  if (requestId == deviceLastMaxResetCommandId) return;

  deviceLastMaxResetCommandId = requestId;
  if (preferencesOk) preferences.putString("cmd_max", requestId);

  commandStatusSenden("max_values_reset", requestId, "resetting");
  deviceMaxReset();
  commandStatusSenden("max_values_reset", requestId, "completed", "Maximalwerte vom ESP32 neu gestartet.");
}
