#pragma once

#include <LittleFS.h>
#include <FS.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <esp32-hal-psram.h>
#include <stddef.h>

// ==================================================
// V5.9.12 - STROMAUSFALLSICHERER OFFLINE-RENNSPEICHER
// ==================================================
// Grundsatz:
// - Online ohne Rueckstau: Messpunkt direkt mit eindeutiger ID zu Firebase.
// - Kein Netz / HTTP-Fehler / vorhandener Rueckstau: zuerst dauerhaft in LittleFS.
// - Nach Verbindung: aelteste Datensaetze geordnet nachsenden.
// - PUT mit deterministischer Sample-ID macht Wiederholungen idempotent.
// - Flash ist die verbindliche Quelle. PSRAM ist nur schneller Zusatz-Cache.
// ==================================================

constexpr uint32_t OFFLINE_RECORD_MAGIC = 0x4D463531UL; // "MF51"
constexpr uint16_t OFFLINE_RECORD_VERSION = 1;
constexpr uint8_t OFFLINE_STATE_PENDING = 0xA5;
constexpr uint8_t OFFLINE_STATE_SENT = 0x5A;
constexpr size_t OFFLINE_FLASH_RESERVE_BYTES = 96UL * 1024UL;
constexpr unsigned long OFFLINE_DRAIN_OK_INTERVAL_MS = 250UL;
constexpr unsigned long OFFLINE_DRAIN_ERROR_BACKOFF_MS = 3000UL;
constexpr size_t OFFLINE_PSRAM_CACHE_RECORDS = 256;

constexpr uint16_t OFFLINE_FLAG_GPS_VALID = 1u << 0;
constexpr uint16_t OFFLINE_FLAG_SPEED_VALID = 1u << 1;
constexpr uint16_t OFFLINE_FLAG_RPM_VALID = 1u << 2;
constexpr uint16_t OFFLINE_FLAG_OIL_PRESSURE_VALID = 1u << 3;
constexpr uint16_t OFFLINE_FLAG_OIL_TEMP_VALID = 1u << 4;
constexpr uint16_t OFFLINE_FLAG_BATTERY_VALID = 1u << 5;
constexpr uint16_t OFFLINE_FLAG_CYLINDER_VALID = 1u << 6;
constexpr uint16_t OFFLINE_FLAG_SWITCH_OUTPUT = 1u << 7;
constexpr uint16_t OFFLINE_FLAG_HDOP_VALID = 1u << 8;
constexpr uint16_t OFFLINE_FLAG_SATELLITES_VALID = 1u << 9;
constexpr uint16_t OFFLINE_FLAG_CAPTURE_TIME_VALID = 1u << 10;

#pragma pack(push, 1)
struct OfflineRaceRecord {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t bootId;
  uint32_t sequence;
  uint32_t capturedMillis;
  uint64_t capturedEpochMs;
  int32_t latE6;
  int32_t lngE6;
  int16_t speedDeci;
  uint16_t rpmValue;
  int16_t oilPressureCenti;
  int16_t oilTempDeci;
  uint16_t batteryCenti;
  int16_t cylinderTempDeci;
  int16_t wifiRssi;
  uint16_t hdopCenti;
  uint8_t satellites;
  uint16_t flags;
  uint8_t reserved;
  uint32_t crc32;
  uint8_t state;
  uint8_t padding[3];
};
#pragma pack(pop)

static_assert(sizeof(OfflineRaceRecord) == 64, "OfflineRaceRecord muss exakt 64 Byte gross sein");

bool offlineBufferReady = false;
bool offlineBufferFull = false;
bool offlinePsramAvailable = false;
size_t offlineFsTotalBytes = 0;
size_t offlineFsUsedBytes = 0;
size_t offlinePsramBytes = 0;
uint32_t offlinePendingCount = 0;
uint32_t offlineQueuedCount = 0;
uint32_t offlineReplayedCount = 0;
uint32_t offlineDroppedCount = 0;
uint32_t offlineCorruptCount = 0;
uint32_t offlineBootId = 0;
uint32_t offlineSampleSequence = 0;
String offlineLastError = "";
String offlineDrainPath = "";
size_t offlineDrainOffset = 0;
unsigned long offlineLastDrainAttempt = 0;

OfflineRaceRecord* offlinePsramCache = nullptr;
size_t offlinePsramCacheWrite = 0;
size_t offlinePsramCacheCount = 0;

uint32_t offlineCrc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
    }
  }
  return ~crc;
}

uint32_t offlineRecordCrc(const OfflineRaceRecord& rec) {
  return offlineCrc32(
    reinterpret_cast<const uint8_t*>(&rec),
    offsetof(OfflineRaceRecord, crc32)
  );
}

bool offlineRecordGueltig(const OfflineRaceRecord& rec) {
  return rec.magic == OFFLINE_RECORD_MAGIC &&
         rec.version == OFFLINE_RECORD_VERSION &&
         rec.size == sizeof(OfflineRaceRecord) &&
         rec.crc32 == offlineRecordCrc(rec);
}

bool offlineRaceIdGueltig(const String& raceId) {
  if (raceId.length() < 5 || raceId.length() > 48) return false;
  for (size_t i = 0; i < raceId.length(); ++i) {
    const char c = raceId[i];
    const bool ok =
      (c >= 'a' && c <= 'z') ||
      (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9') ||
      c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

String offlineQueuePfad(const String& raceId) {
  return String("/rq_") + raceId + ".bin";
}

String offlineRaceIdAusPfad(const String& rawPath) {
  String path = rawPath;
  if (!path.startsWith("/")) path = "/" + path;
  if (!path.startsWith("/rq_") || !path.endsWith(".bin")) return "";
  return path.substring(4, path.length() - 4);
}

String offlineSampleId(const OfflineRaceRecord& rec) {
  char id[32];
  snprintf(
    id,
    sizeof(id),
    "b%08lx_s%08lx",
    (unsigned long)rec.bootId,
    (unsigned long)rec.sequence
  );
  return String(id);
}

String offlineUInt64String(uint64_t value) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
  return String(buf);
}

int16_t offlineSkaliertSigned(float value, float faktor) {
  if (!isfinite(value)) return 0;
  long v = lroundf(value * faktor);
  if (v < INT16_MIN) v = INT16_MIN;
  if (v > INT16_MAX) v = INT16_MAX;
  return (int16_t)v;
}

uint16_t offlineSkaliertUnsigned(float value, float faktor) {
  if (!isfinite(value) || value <= 0.0f) return 0;
  long v = lroundf(value * faktor);
  if (v < 0) v = 0;
  if (v > 65535L) v = 65535L;
  return (uint16_t)v;
}

void offlinePsramCacheSpeichern(const OfflineRaceRecord& rec) {
  if (!offlinePsramCache) return;
  offlinePsramCache[offlinePsramCacheWrite] = rec;
  offlinePsramCacheWrite =
    (offlinePsramCacheWrite + 1) % OFFLINE_PSRAM_CACHE_RECORDS;
  if (offlinePsramCacheCount < OFFLINE_PSRAM_CACHE_RECORDS) {
    offlinePsramCacheCount++;
  }
}

void offlineFsStatusAktualisieren() {
  if (!offlineBufferReady) return;
  offlineFsTotalBytes = LittleFS.totalBytes();
  offlineFsUsedBytes = LittleFS.usedBytes();
  const size_t frei =
    offlineFsTotalBytes > offlineFsUsedBytes
      ? offlineFsTotalBytes - offlineFsUsedBytes
      : 0;
  offlineBufferFull = frei < (OFFLINE_FLASH_RESERVE_BYTES + sizeof(OfflineRaceRecord));
}

bool offlineDateiErstenPendingOffset(
  const String& path,
  size_t& offsetOut,
  OfflineRaceRecord* recordOut = nullptr
) {
  File file = LittleFS.open(path, "r");
  if (!file) return false;

  size_t offset = 0;
  OfflineRaceRecord rec;

  while (file.available() >= (int)sizeof(rec)) {
    if (file.read(reinterpret_cast<uint8_t*>(&rec), sizeof(rec)) != sizeof(rec)) {
      break;
    }

    if (!offlineRecordGueltig(rec)) {
      offset += sizeof(rec);
      continue;
    }

    if (rec.state != OFFLINE_STATE_SENT) {
      offsetOut = offset;
      if (recordOut) *recordOut = rec;
      file.close();
      return true;
    }

    offset += sizeof(rec);
  }

  file.close();
  return false;
}

bool offlineNaechsteDateiWaehlen() {
  if (!offlineBufferReady) return false;

  File root = LittleFS.open("/");
  if (!root) return false;

  String beste = "";
  File entry = root.openNextFile();
  while (entry) {
    String name = entry.name();
    entry.close();
    if (!name.startsWith("/")) name = "/" + name;

    if (offlineRaceIdAusPfad(name).length() > 0) {
      size_t firstPending = 0;
      if (offlineDateiErstenPendingOffset(name, firstPending, nullptr)) {
        if (beste.length() == 0 || name < beste) {
          beste = name;
        }
      }
    }

    entry = root.openNextFile();
  }
  root.close();

  if (beste.length() == 0) {
    offlineDrainPath = "";
    offlineDrainOffset = 0;
    return false;
  }

  offlineDrainPath = beste;
  size_t firstPending = 0;
  if (!offlineDateiErstenPendingOffset(beste, firstPending, nullptr)) {
    offlineDrainPath = "";
    return false;
  }
  offlineDrainOffset = firstPending;
  return true;
}

bool offlineRecordLesen(
  const String& path,
  size_t offset,
  OfflineRaceRecord& rec
) {
  File file = LittleFS.open(path, "r");
  if (!file) return false;
  if (!file.seek(offset, SeekSet)) {
    file.close();
    return false;
  }
  const size_t gelesen = file.read(reinterpret_cast<uint8_t*>(&rec), sizeof(rec));
  file.close();
  return gelesen == sizeof(rec) && offlineRecordGueltig(rec);
}

bool offlineRecordAlsGesendetMarkieren(const String& path, size_t offset) {
  File file = LittleFS.open(path, "r+");
  if (!file) return false;

  const size_t statusOffset = offset + offsetof(OfflineRaceRecord, state);
  if (!file.seek(statusOffset, SeekSet)) {
    file.close();
    return false;
  }

  const uint8_t status = OFFLINE_STATE_SENT;
  const size_t geschrieben = file.write(&status, 1);
  file.flush();
  file.close();
  return geschrieben == 1;
}

void offlineLeereQueueDateienAufraeumen() {
  if (!offlineBufferReady) return;

  File root = LittleFS.open("/");
  if (!root) return;

  String loeschen[8];
  size_t anzahl = 0;

  File entry = root.openNextFile();
  while (entry) {
    String name = entry.name();
    entry.close();
    if (!name.startsWith("/")) name = "/" + name;

    if (offlineRaceIdAusPfad(name).length() > 0 && anzahl < 8) {
      size_t dummy = 0;
      if (!offlineDateiErstenPendingOffset(name, dummy, nullptr)) {
        loeschen[anzahl++] = name;
      }
    }
    entry = root.openNextFile();
  }
  root.close();

  for (size_t i = 0; i < anzahl; ++i) {
    LittleFS.remove(loeschen[i]);
  }
  offlineFsStatusAktualisieren();
}

void offlineUnvollstaendigeDateiEndenReparieren() {
  if (!offlineBufferReady) return;

  File root = LittleFS.open("/");
  if (!root) return;

  String pfade[8];
  size_t groessen[8];
  size_t anzahl = 0;

  File entry = root.openNextFile();
  while (entry && anzahl < 8) {
    String name = entry.name();
    const size_t size = entry.size();
    entry.close();
    if (!name.startsWith("/")) name = "/" + name;

    if (offlineRaceIdAusPfad(name).length() > 0 &&
        (size % sizeof(OfflineRaceRecord)) != 0) {
      pfade[anzahl] = name;
      groessen[anzahl] = size - (size % sizeof(OfflineRaceRecord));
      anzahl++;
    }
    entry = root.openNextFile();
  }
  root.close();

  uint8_t block[256];

  for (size_t i = 0; i < anzahl; ++i) {
    const String tempPfad = pfade[i] + ".repair";
    LittleFS.remove(tempPfad);

    File quelle = LittleFS.open(pfade[i], "r");
    File ziel = LittleFS.open(tempPfad, "w");
    if (!quelle || !ziel) {
      if (quelle) quelle.close();
      if (ziel) ziel.close();
      LittleFS.remove(tempPfad);
      offlineLastError = "Unvollstaendiges Queue-Dateiende konnte nicht repariert werden";
      continue;
    }

    size_t verbleibend = groessen[i];
    bool ok = true;
    while (verbleibend > 0) {
      const size_t teil = verbleibend > sizeof(block) ? sizeof(block) : verbleibend;
      const size_t gelesen = quelle.read(block, teil);
      if (gelesen != teil || ziel.write(block, teil) != teil) {
        ok = false;
        break;
      }
      verbleibend -= teil;
    }

    ziel.flush();
    quelle.close();
    ziel.close();

    if (!ok || verbleibend != 0) {
      LittleFS.remove(tempPfad);
      offlineLastError = "Unvollstaendiges Queue-Dateiende konnte nicht kopiert werden";
      continue;
    }

    // Erst wenn die Reparaturdatei komplett geschrieben ist, das Original ersetzen.
    if (!LittleFS.remove(pfade[i]) || !LittleFS.rename(tempPfad, pfade[i])) {
      offlineLastError = "Reparierte Queue-Datei konnte nicht aktiviert werden";
      continue;
    }

    offlineCorruptCount++;
  }
}

void offlineQueueScannen() {
  offlinePendingCount = 0;
  offlineCorruptCount = 0;
  offlineDrainPath = "";
  offlineDrainOffset = 0;

  if (!offlineBufferReady) return;

  File root = LittleFS.open("/");
  if (!root) {
    offlineLastError = "LittleFS-Wurzel konnte nicht gelesen werden";
    return;
  }

  File entry = root.openNextFile();
  while (entry) {
    String name = entry.name();
    if (!name.startsWith("/")) name = "/" + name;

    if (offlineRaceIdAusPfad(name).length() > 0) {
      OfflineRaceRecord rec;
      while (entry.available() >= (int)sizeof(rec)) {
        if (entry.read(reinterpret_cast<uint8_t*>(&rec), sizeof(rec)) != sizeof(rec)) {
          break;
        }
        if (!offlineRecordGueltig(rec)) {
          offlineCorruptCount++;
          continue;
        }
        if (rec.state != OFFLINE_STATE_SENT) {
          offlinePendingCount++;
        }
      }

      if (entry.available() > 0) {
        offlineCorruptCount++;
      }
    }

    entry.close();
    entry = root.openNextFile();
  }
  root.close();

  offlineFsStatusAktualisieren();
  if (offlinePendingCount == 0) {
    offlineLeereQueueDateienAufraeumen();
  }
}

void offlinePufferInitialisieren() {
  offlineBufferReady = false;
  offlineLastError = "";

  const esp_partition_t* dataPartition = esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA,
    ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
    "spiffs"
  );

  if (!dataPartition || dataPartition->size < 256UL * 1024UL) {
    offlineLastError = "Keine geeignete SPIFFS/LittleFS-Datenpartition gefunden";
    Serial.print("OFFLINE-PUFFER: FEHLER - ");
    Serial.println(offlineLastError);
    return;
  }

  if (!LittleFS.begin(false, "/littlefs", 5, "spiffs")) {
    Serial.println("OFFLINE-PUFFER: Datenpartition noch nicht formatiert - initialisiere LittleFS einmalig.");
    if (!LittleFS.format() || !LittleFS.begin(false, "/littlefs", 5, "spiffs")) {
      offlineLastError = "LittleFS konnte nicht initialisiert werden";
      Serial.print("OFFLINE-PUFFER: FEHLER - ");
      Serial.println(offlineLastError);
      return;
    }
  }

  offlineBufferReady = true;
  offlineFsStatusAktualisieren();

  offlinePsramAvailable = psramFound();
  offlinePsramBytes = offlinePsramAvailable ? ESP.getPsramSize() : 0;

  if (offlinePsramAvailable) {
    offlinePsramCache = static_cast<OfflineRaceRecord*>(
      ps_malloc(sizeof(OfflineRaceRecord) * OFFLINE_PSRAM_CACHE_RECORDS)
    );
    if (!offlinePsramCache) {
      offlinePsramAvailable = false;
      offlinePsramBytes = 0;
    }
  }

  if (preferencesOk) {
    uint32_t alt = preferences.getULong("bootid", 0);
    offlineBootId = alt + 1UL;
    if (offlineBootId == 0) offlineBootId = 1;
    preferences.putULong("bootid", offlineBootId);
  } else {
    offlineBootId = esp_random();
    if (offlineBootId == 0) offlineBootId = 1;
  }

  offlineUnvollstaendigeDateiEndenReparieren();
  offlineQueueScannen();

  Serial.print("OFFLINE-PUFFER: OK | Flash ");
  Serial.print((unsigned long)offlineFsUsedBytes);
  Serial.print("/");
  Serial.print((unsigned long)offlineFsTotalBytes);
  Serial.print(" Byte | pending ");
  Serial.print(offlinePendingCount);
  Serial.print(" | PSRAM ");
  if (offlinePsramAvailable) {
    Serial.print((unsigned long)offlinePsramBytes);
    Serial.println(" Byte");
  } else {
    Serial.println("nicht verfuegbar - Flash-Puffer bleibt voll funktionsfaehig");
  }
}

OfflineRaceRecord offlineRecordBauen() {
  OfflineRaceRecord rec = {};
  rec.magic = OFFLINE_RECORD_MAGIC;
  rec.version = OFFLINE_RECORD_VERSION;
  rec.size = sizeof(OfflineRaceRecord);
  rec.bootId = offlineBootId;
  rec.sequence = ++offlineSampleSequence;
  rec.capturedMillis = millis();
  rec.state = OFFLINE_STATE_PENDING;

  // Snapshot the diagnostic window using the same deterministic sample key.
  mf35xOilDiagCapture(rec.bootId, rec.sequence);

  const GpsSnapshot gpsDaten = gpsSnapshotLesen();
  const bool gpsGueltig = gpsFixAktuell(gpsDaten);

  if (gpsGueltig) {
    rec.flags |= OFFLINE_FLAG_GPS_VALID;
    rec.latE6 = (int32_t)llround(gpsDaten.lat * 1000000.0);
    rec.lngE6 = (int32_t)llround(gpsDaten.lng * 1000000.0);
  }

  if (gpsGueltig && gpsDaten.speedValid) {
    rec.flags |= OFFLINE_FLAG_SPEED_VALID;
    rec.speedDeci = offlineSkaliertSigned((float)gpsDaten.speedKmh, 10.0f);
  }

  if (gpsDaten.hdopValid) {
    rec.flags |= OFFLINE_FLAG_HDOP_VALID;
    rec.hdopCenti = offlineSkaliertUnsigned((float)gpsDaten.hdop, 100.0f);
  }

  if (gpsDaten.satellitesValid) {
    rec.flags |= OFFLINE_FLAG_SATELLITES_VALID;
    rec.satellites = (uint8_t)(gpsDaten.satellites > 255U ? 255U : gpsDaten.satellites);
  }

  if (gpsDaten.utcValid && gpsDaten.utcEpochMs > 1700000000000ULL) {
    const unsigned long delta =
      (unsigned long)(rec.capturedMillis - gpsDaten.utcUpdateMillis);
    if (delta <= 10000UL) {
      rec.capturedEpochMs = gpsDaten.utcEpochMs + (uint64_t)delta;
      rec.flags |= OFFLINE_FLAG_CAPTURE_TIME_VALID;
    }
  }

  if (rpmSignalOk && isfinite(rpm)) {
    rec.flags |= OFFLINE_FLAG_RPM_VALID;
    rec.rpmValue = offlineSkaliertUnsigned(rpm, 1.0f);
  }

  if (isfinite(oilPressureBar)) {
    rec.flags |= OFFLINE_FLAG_OIL_PRESSURE_VALID;
    rec.oilPressureCenti = offlineSkaliertSigned(oilPressureBar, 100.0f);
  }

  if (isfinite(oilTemp)) {
    rec.flags |= OFFLINE_FLAG_OIL_TEMP_VALID;
    rec.oilTempDeci = offlineSkaliertSigned(oilTemp, 10.0f);
  }

  if (isfinite(batteryVoltage)) {
    rec.flags |= OFFLINE_FLAG_BATTERY_VALID;
    rec.batteryCenti = offlineSkaliertUnsigned(batteryVoltage, 100.0f);
  }

  if (isfinite(cylinderTemp)) {
    rec.flags |= OFFLINE_FLAG_CYLINDER_VALID;
    rec.cylinderTempDeci = offlineSkaliertSigned((float)cylinderTemp, 10.0f);
  }

  if (schaltausgangAktiv) {
    rec.flags |= OFFLINE_FLAG_SWITCH_OUTPUT;
  }

  rec.wifiRssi = WiFi.status() == WL_CONNECTED ? (int16_t)WiFi.RSSI() : (int16_t)-127;
  rec.crc32 = offlineRecordCrc(rec);
  return rec;
}

String offlineRecordJson(const OfflineRaceRecord& rec, bool replay) {
  String json = "{";
  json.reserve(900);
  bool first = true;

  const bool gpsValid = (rec.flags & OFFLINE_FLAG_GPS_VALID) != 0;
  const bool speedValid = (rec.flags & OFFLINE_FLAG_SPEED_VALID) != 0;
  const bool rpmValid = (rec.flags & OFFLINE_FLAG_RPM_VALID) != 0;

  jsonBoolFeld(json, first, "gps_valid", gpsValid);

  if (gpsValid) {
    jsonFloatFeld(json, first, "lat", (double)rec.latE6 / 1000000.0, 6);
    jsonFloatFeld(json, first, "lng", (double)rec.lngE6 / 1000000.0, 6);
  } else {
    jsonRaw(json, first, "lat", "null");
    jsonRaw(json, first, "lng", "null");
  }

  if (speedValid) {
    jsonFloatFeld(json, first, "speed_kmh", (double)rec.speedDeci / 10.0, 1);
  } else {
    jsonFloatFeld(json, first, "speed_kmh", 0.0, 1);
  }

  if ((rec.flags & OFFLINE_FLAG_HDOP_VALID) != 0) {
    jsonFloatFeld(json, first, "hdop", (double)rec.hdopCenti / 100.0, 2);
  } else {
    jsonRaw(json, first, "hdop", "null");
  }

  if ((rec.flags & OFFLINE_FLAG_SATELLITES_VALID) != 0) {
    jsonULongFeld(json, first, "satellites", rec.satellites);
  } else {
    jsonULongFeld(json, first, "satellites", 0UL);
  }

  if (rpmValid) {
    jsonULongFeld(json, first, "rpm", rec.rpmValue);
  } else {
    jsonRaw(json, first, "rpm", "null");
  }

  if ((rec.flags & OFFLINE_FLAG_OIL_PRESSURE_VALID) != 0) {
    jsonFloatFeld(json, first, "oil_pressure", (double)rec.oilPressureCenti / 100.0, 2);
  } else {
    jsonRaw(json, first, "oil_pressure", "null");
  }

  // Companion data never changes the legacy 64-byte OfflineRaceRecord layout.
  mf35xOilDiagAppendJson(json, first, rec.bootId, rec.sequence, replay);

  if ((rec.flags & OFFLINE_FLAG_OIL_TEMP_VALID) != 0) {
    jsonFloatFeld(json, first, "oil_temp", (double)rec.oilTempDeci / 10.0, 1);
  } else {
    jsonRaw(json, first, "oil_temp", "null");
  }

  if ((rec.flags & OFFLINE_FLAG_BATTERY_VALID) != 0) {
    jsonFloatFeld(json, first, "battery_v", (double)rec.batteryCenti / 100.0, 2);
  } else {
    jsonRaw(json, first, "battery_v", "null");
  }

  if ((rec.flags & OFFLINE_FLAG_CYLINDER_VALID) != 0) {
    jsonFloatFeld(json, first, "cylinder_temp", (double)rec.cylinderTempDeci / 10.0, 1);
  } else {
    jsonRaw(json, first, "cylinder_temp", "null");
  }

  jsonBoolFeld(json, first, "switch_output", (rec.flags & OFFLINE_FLAG_SWITCH_OUTPUT) != 0);
  jsonRaw(json, first, "wifi_rssi", String(rec.wifiRssi));
  jsonULongFeld(json, first, "uptime_seconds", rec.capturedMillis / 1000UL);
  jsonText(json, first, "sample_id", offlineSampleId(rec));
  jsonULongFeld(json, first, "sample_boot_id", rec.bootId);
  jsonULongFeld(json, first, "sample_sequence", rec.sequence);
  jsonULongFeld(json, first, "captured_uptime_ms", rec.capturedMillis);
  jsonBoolFeld(json, first, "buffered_replay", replay);

  const bool zeitGueltig =
    (rec.flags & OFFLINE_FLAG_CAPTURE_TIME_VALID) != 0 &&
    rec.capturedEpochMs > 1700000000000ULL;

  jsonBoolFeld(json, first, "capture_time_valid", zeitGueltig);
  jsonText(json, first, "timestamp_source", zeitGueltig ? "gps_utc" : "firebase_replay");

  if (zeitGueltig) {
    jsonRaw(json, first, "timestamp", offlineUInt64String(rec.capturedEpochMs));
  } else {
    jsonRaw(json, first, "timestamp", "{\".sv\":\"timestamp\"}");
  }

  json += '}';
  return json;
}

bool offlineRecordSenden(
  const String& raceId,
  const OfflineRaceRecord& rec,
  bool replay
) {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!offlineRaceIdGueltig(raceId)) return false;

  String pfad = "tracker/races/";
  pfad += raceId;
  pfad += "/samples/";
  pfad += offlineSampleId(rec);

  return firebasePut(pfad, offlineRecordJson(rec, replay));
}

bool offlineRecordDauerhaftPuffern(
  const String& raceId,
  const OfflineRaceRecord& rec
) {
  if (!offlineBufferReady) {
    offlineDroppedCount++;
    offlineLastError = "Offline-Puffer nicht bereit";
    return false;
  }

  if (!offlineRaceIdGueltig(raceId)) {
    offlineDroppedCount++;
    offlineLastError = "Ungueltige Renn-ID fuer Offline-Puffer";
    return false;
  }

  offlineFsStatusAktualisieren();
  if (offlineBufferFull) {
    offlineDroppedCount++;
    offlineLastError = "Offline-Puffer voll - neuer Messpunkt verworfen";
    return false;
  }

  const String path = offlineQueuePfad(raceId);
  File file = LittleFS.open(path, FILE_APPEND);
  if (!file) {
    offlineDroppedCount++;
    offlineLastError = "Queue-Datei konnte nicht geoeffnet werden";
    return false;
  }

  const size_t geschrieben =
    file.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(rec));
  file.flush();
  file.close();

  if (geschrieben != sizeof(rec)) {
    offlineDroppedCount++;
    offlineLastError = "Queue-Schreibvorgang unvollstaendig";
    return false;
  }

  offlinePendingCount++;
  offlineQueuedCount++;
  offlinePsramCacheSpeichern(rec);
  // Main race data has priority. Diagnostic companion failure must never make
  // a valid 64-byte race record fail or disappear.
  mf35xOilDiagPersist(rec.bootId, rec.sequence);
  offlineFsStatusAktualisieren();
  return true;
}

void offlineRennMesspunktBearbeiten() {
  const OfflineRaceRecord rec = offlineRecordBauen();

  // Solange kein Rueckstau existiert, direkt senden. Ein HTTP-Timeout kann
  // nicht zu einem Duplikat fuehren, weil dieselbe deterministische ID bei
  // einem spaeteren Versuch per PUT ueberschrieben wird.
  if (WiFi.status() == WL_CONNECTED && offlinePendingCount == 0) {
    if (offlineRecordSenden(recordingConfig.raceId, rec, false)) {
      historyOk++;
      mf35xOilDiagMarkDelivered(rec.bootId, rec.sequence, true);
      return;
    }
    historyFehler++;
  }

  if (!offlineRecordDauerhaftPuffern(recordingConfig.raceId, rec)) {
    historyFehler++;
  }
}

void offlineDrainBearbeiten() {
  if (!offlineBufferReady || offlinePendingCount == 0) return;
  if (WiFi.status() != WL_CONNECTED) return;

  const unsigned long jetzt = millis();
  const unsigned long intervall =
    offlineLastError.length() > 0
      ? OFFLINE_DRAIN_ERROR_BACKOFF_MS
      : OFFLINE_DRAIN_OK_INTERVAL_MS;

  if (!zeitFaellig(jetzt, offlineLastDrainAttempt, intervall)) return;
  offlineLastDrainAttempt = jetzt;

  if (offlineDrainPath.length() == 0 && !offlineNaechsteDateiWaehlen()) {
    offlineQueueScannen();
    return;
  }

  OfflineRaceRecord rec;
  if (!offlineRecordLesen(offlineDrainPath, offlineDrainOffset, rec)) {
    // Entweder Dateiende, bereits gesendeter/ungueltiger Datensatz oder Datei
    // wurde zwischenzeitlich aufgeraeumt. Naechsten pending Datensatz suchen.
    size_t nextOffset = 0;
    if (offlineDateiErstenPendingOffset(offlineDrainPath, nextOffset, &rec)) {
      offlineDrainOffset = nextOffset;
    } else {
      LittleFS.remove(offlineDrainPath);
      offlineDrainPath = "";
      offlineDrainOffset = 0;
      offlineFsStatusAktualisieren();
      return;
    }
  }

  if (rec.state == OFFLINE_STATE_SENT) {
    offlineDrainOffset += sizeof(OfflineRaceRecord);
    return;
  }

  const String raceId = offlineRaceIdAusPfad(offlineDrainPath);
  if (raceId.length() == 0) {
    offlineLastError = "Queue-Dateiname enthaelt keine gueltige Renn-ID";
    offlineDrainPath = "";
    return;
  }

  if (!offlineRecordSenden(raceId, rec, true)) {
    historyFehler++;
    offlineLastError = "Nachsenden zu Firebase fehlgeschlagen";
    return;
  }

  // Erst NACH HTTP 200 dauerhaft als gesendet markieren. Falls zwischen
  // HTTP-Erfolg und Markierung der Strom ausfaellt, wird derselbe PUT spaeter
  // wiederholt - dank deterministischer ID ohne Duplikat.
  if (!offlineRecordAlsGesendetMarkieren(offlineDrainPath, offlineDrainOffset)) {
    offlineLastError = "Gesendeter Queue-Eintrag konnte nicht bestaetigt werden";
    return;
  }

  if (offlinePendingCount > 0) offlinePendingCount--;
  mf35xOilDiagMarkDelivered(rec.bootId, rec.sequence, offlinePendingCount == 0);
  offlineReplayedCount++;
  historyOk++;
  offlineLastError = "";
  offlineDrainOffset += sizeof(OfflineRaceRecord);

  if (offlinePendingCount == 0) {
    offlineDrainPath = "";
    offlineDrainOffset = 0;
    offlineLeereQueueDateienAufraeumen();
  }
}
