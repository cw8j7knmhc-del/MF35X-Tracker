#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <LittleFS.h>
#include <stddef.h>
#include <string.h>

// ==================================================
// NAECHSTER USB-STAND - PUNKT 5
// SCHNELLER GPS-/FAHRZUSTAND-KANAL FUER DIE RENNSTRECKE
// ==================================================
// Das GPS-Modul darf intern mit 10, 20 oder spaeter 50 Hz laufen.
// Die normale Voll-Telemetrie bleibt bewusst langsam (z. B. 5 s).
// Fuer Streckenlinie/Fahrzustand wird parallel mit 10 Hz aufgezeichnet:
//   Position, Geschwindigkeit, RPM, GPIO11, HDOP, Satelliten und Zeit.
// Je 10 Punkte werden zu einem ~1-s-Paket zusammengefasst. Dadurch entsteht
// trotz 10-Hz-Track nur etwa ein Firebase-PUT pro Sekunde.
//
// Wichtig fuer ein spaeteres 50-Hz-Modul:
// - Die GPS-Empfangsrate und die Track-Speicherrate sind getrennt.
// - Der Logger nimmt alle 100 ms den jeweils neuesten gueltigen Fix.
// - 50 Hz intern bleiben fuer schnelle/saubere Positions- und Speed-Updates
//   nutzbar, ohne 50 Punkte/s dauerhaft speichern zu muessen.
// - Bei ~70 km/h ergeben 10 Hz etwa 1,9 m Weg zwischen Trackpunkten.
// ==================================================

constexpr uint32_t MF35X_FAST_TRACK_INTERVAL_MS = 100UL; // 10 Hz
constexpr uint8_t MF35X_FAST_TRACK_POINTS_PER_BATCH = 10;
constexpr uint8_t MF35X_FAST_TRACK_RAM_BATCHES = 30;     // ca. 30 s RAM-Puffer
constexpr uint32_t MF35X_FAST_TRACK_TASK_STACK = 4096;
constexpr UBaseType_t MF35X_FAST_TRACK_TASK_PRIORITY = 2;
constexpr BaseType_t MF35X_FAST_TRACK_TASK_CORE = 1;
constexpr TickType_t MF35X_FAST_TRACK_POLL_TICKS = pdMS_TO_TICKS(5);
constexpr unsigned long MF35X_FAST_TRACK_REPLAY_MS = 150UL;
constexpr size_t MF35X_FAST_TRACK_FLASH_PROTECT_BYTES = 1024UL * 1024UL;

constexpr uint32_t MF35X_FAST_TRACK_MAGIC = 0x4D463546UL; // "MF5F"
constexpr uint16_t MF35X_FAST_TRACK_VERSION = 1;
constexpr uint8_t MF35X_FAST_TRACK_PENDING = 0xA5;
constexpr uint8_t MF35X_FAST_TRACK_SENT = 0x5A;

constexpr uint8_t MF35X_FAST_FLAG_GPS_VALID = 1u << 0;
constexpr uint8_t MF35X_FAST_FLAG_SPEED_VALID = 1u << 1;
constexpr uint8_t MF35X_FAST_FLAG_RPM_VALID = 1u << 2;
constexpr uint8_t MF35X_FAST_FLAG_GPIO11 = 1u << 3;
constexpr uint8_t MF35X_FAST_FLAG_HDOP_VALID = 1u << 4;
constexpr uint8_t MF35X_FAST_FLAG_SATS_VALID = 1u << 5;
constexpr uint8_t MF35X_FAST_FLAG_TIME_VALID = 1u << 6;

#pragma pack(push, 1)
struct Mf35xFastTrackPoint {
  uint16_t deltaMs;
  int32_t latE6;
  int32_t lngE6;
  int16_t speedDeci;
  uint16_t rpmValue;
  uint16_t hdopCenti;
  uint8_t satellites;
  uint8_t flags;
};

struct Mf35xFastTrackBatch {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t bootId;
  uint32_t batchSequence;
  uint32_t firstMillis;
  uint64_t firstEpochMs;
  uint8_t count;
  uint8_t reserved[3];
  Mf35xFastTrackPoint points[MF35X_FAST_TRACK_POINTS_PER_BATCH];
  uint32_t crc32;
  uint8_t state;
  uint8_t padding[3];
};
#pragma pack(pop)

struct Mf35xFastTrackQueueItem {
  Mf35xFastTrackBatch batch;
  char raceId[MF35X_RACE_ID_BUFFER_LEN];
};

QueueHandle_t mf35xFastTrackQueue = nullptr;
TaskHandle_t mf35xFastTrackTaskHandle = nullptr;
volatile uint32_t mf35xFastTrackCapturedPoints = 0;
volatile uint32_t mf35xFastTrackQueuedBatches = 0;
volatile uint32_t mf35xFastTrackUploadedBatches = 0;
volatile uint32_t mf35xFastTrackBufferedBatches = 0;
volatile uint32_t mf35xFastTrackReplayedBatches = 0;
volatile uint32_t mf35xFastTrackDroppedBatches = 0;
volatile uint32_t mf35xFastTrackDuplicateFixSkips = 0;
volatile uint32_t mf35xFastTrackInvalidFixPoints = 0;
uint32_t mf35xFastTrackBatchSequence = 0;
unsigned long mf35xFastTrackLastReplayMs = 0;
String mf35xFastTrackReplayPath = "";
size_t mf35xFastTrackReplayOffset = 0;

uint32_t mf35xFastTrackCrc(const Mf35xFastTrackBatch& batch) {
  return offlineCrc32(
    reinterpret_cast<const uint8_t*>(&batch),
    offsetof(Mf35xFastTrackBatch, crc32)
  );
}

bool mf35xFastTrackBatchValid(const Mf35xFastTrackBatch& batch) {
  return batch.magic == MF35X_FAST_TRACK_MAGIC &&
         batch.version == MF35X_FAST_TRACK_VERSION &&
         batch.size == sizeof(Mf35xFastTrackBatch) &&
         batch.count > 0 &&
         batch.count <= MF35X_FAST_TRACK_POINTS_PER_BATCH &&
         batch.crc32 == mf35xFastTrackCrc(batch);
}

String mf35xFastTrackFilePath(const String& raceId) {
  return String("/fg_") + raceId + ".bin";
}

String mf35xFastTrackRaceIdFromPath(String path) {
  if (!path.startsWith("/")) path = "/" + path;
  if (!path.startsWith("/fg_") || !path.endsWith(".bin")) return "";
  return path.substring(4, path.length() - 4);
}

String mf35xFastTrackBatchId(const Mf35xFastTrackBatch& batch) {
  char id[40];
  snprintf(
    id,
    sizeof(id),
    "fb%08lx_b%08lx",
    (unsigned long)batch.bootId,
    (unsigned long)batch.batchSequence
  );
  return String(id);
}

void mf35xFastTrackBatchReset(Mf35xFastTrackBatch& batch) {
  memset(&batch, 0, sizeof(batch));
  batch.magic = MF35X_FAST_TRACK_MAGIC;
  batch.version = MF35X_FAST_TRACK_VERSION;
  batch.size = sizeof(Mf35xFastTrackBatch);
  batch.bootId = offlineBootId;
  batch.state = MF35X_FAST_TRACK_PENDING;
}

void mf35xFastTrackFinalize(Mf35xFastTrackBatch& batch) {
  batch.batchSequence = ++mf35xFastTrackBatchSequence;
  if (batch.batchSequence == 0) batch.batchSequence = ++mf35xFastTrackBatchSequence;
  batch.crc32 = mf35xFastTrackCrc(batch);
}

bool mf35xFastTrackEnqueue(
  const Mf35xFastTrackBatch& batch,
  const char* raceId
) {
  if (!mf35xFastTrackQueue || !raceId || !raceId[0] || batch.count == 0) return false;

  Mf35xFastTrackQueueItem item = {};
  item.batch = batch;
  strncpy(item.raceId, raceId, sizeof(item.raceId) - 1);

  if (xQueueSend(mf35xFastTrackQueue, &item, 0) != pdTRUE) {
    mf35xFastTrackDroppedBatches++;
    return false;
  }

  mf35xFastTrackQueuedBatches++;
  return true;
}

void mf35xFastTrackCaptureTask(void*) {
  uint32_t lastRevision = 0;
  uint32_t nextCaptureMs = 0;
  uint32_t lastGpsFixMillis = 0;
  char activeRaceId[MF35X_RACE_ID_BUFFER_LEN] = {};
  Mf35xFastTrackBatch batch = {};
  mf35xFastTrackBatchReset(batch);

  for (;;) {
    const Mf35xRaceTimingConfig cfg = mf35xRaceTimingConfigLesen();
    const uint32_t now = millis();

    if (cfg.revision != lastRevision) {
      if (batch.count > 0 && activeRaceId[0]) {
        mf35xFastTrackFinalize(batch);
        mf35xFastTrackEnqueue(batch, activeRaceId);
      }

      lastRevision = cfg.revision;
      nextCaptureMs = cfg.enabled ? now : 0;
      lastGpsFixMillis = 0;
      memset(activeRaceId, 0, sizeof(activeRaceId));
      if (cfg.enabled) strncpy(activeRaceId, cfg.raceId, sizeof(activeRaceId) - 1);
      mf35xFastTrackBatchReset(batch);
    }

    if (cfg.enabled && nextCaptureMs != 0 &&
        (int32_t)(now - nextCaptureMs) >= 0) {
      nextCaptureMs += MF35X_FAST_TRACK_INTERVAL_MS;

      const GpsSnapshot gpsDaten = gpsSnapshotLesen();
      const bool gpsValid = gpsFixAktuell(gpsDaten);

      // Bei 10-Hz- oder 50-Hz-GPS nur neue Positionsfixes speichern. Dadurch
      // entstehen bei Phasenverschiebung zwischen GPS und Logger keine
      // kuenstlich doppelten Streckenpunkte.
      if (gpsValid && gpsDaten.letzterFixMillis == lastGpsFixMillis) {
        mf35xFastTrackDuplicateFixSkips++;
      } else {
        if (batch.count == 0) {
          batch.firstMillis = now;
          batch.firstEpochMs = 0;
          if (gpsDaten.utcValid && gpsDaten.utcEpochMs > 1700000000000ULL) {
            const uint32_t age = (uint32_t)(now - gpsDaten.utcUpdateMillis);
            if (age <= 10000UL) {
              batch.firstEpochMs = gpsDaten.utcEpochMs + (uint64_t)age;
            }
          }
        }

        Mf35xFastTrackPoint& point = batch.points[batch.count];
        memset(&point, 0, sizeof(point));
        const uint32_t delta = (uint32_t)(now - batch.firstMillis);
        point.deltaMs = delta > 65535UL ? 65535U : (uint16_t)delta;

        if (gpsValid) {
          point.flags |= MF35X_FAST_FLAG_GPS_VALID;
          point.latE6 = (int32_t)llround(gpsDaten.lat * 1000000.0);
          point.lngE6 = (int32_t)llround(gpsDaten.lng * 1000000.0);
          lastGpsFixMillis = gpsDaten.letzterFixMillis;
        } else {
          mf35xFastTrackInvalidFixPoints++;
        }

        if (gpsValid && gpsDaten.speedValid) {
          point.flags |= MF35X_FAST_FLAG_SPEED_VALID;
          long s = lround(gpsDaten.speedKmh * 10.0);
          if (s < INT16_MIN) s = INT16_MIN;
          if (s > INT16_MAX) s = INT16_MAX;
          point.speedDeci = (int16_t)s;
        }

        if (rpmSignalOk && isfinite(rpm)) {
          point.flags |= MF35X_FAST_FLAG_RPM_VALID;
          point.rpmValue = offlineSkaliertUnsigned(rpm, 1.0f);
        }

        if (schaltausgangAktiv) point.flags |= MF35X_FAST_FLAG_GPIO11;

        if (gpsDaten.hdopValid) {
          point.flags |= MF35X_FAST_FLAG_HDOP_VALID;
          point.hdopCenti = offlineSkaliertUnsigned((float)gpsDaten.hdop, 100.0f);
        }

        if (gpsDaten.satellitesValid) {
          point.flags |= MF35X_FAST_FLAG_SATS_VALID;
          point.satellites = (uint8_t)(gpsDaten.satellites > 255U ? 255U : gpsDaten.satellites);
        }

        if (batch.firstEpochMs > 1700000000000ULL) {
          point.flags |= MF35X_FAST_FLAG_TIME_VALID;
        }

        batch.count++;
        mf35xFastTrackCapturedPoints++;
      }

      const bool full = batch.count >= MF35X_FAST_TRACK_POINTS_PER_BATCH;
      const bool oneSecondOld = batch.count > 0 &&
        (uint32_t)(now - batch.firstMillis) >= 1000UL;

      if (full || oneSecondOld) {
        mf35xFastTrackFinalize(batch);
        mf35xFastTrackEnqueue(batch, activeRaceId);
        mf35xFastTrackBatchReset(batch);
      }

      // Nicht nachholen, wenn der Task selbst mehr als ein Intervall spaet war.
      if ((int32_t)(now - nextCaptureMs) >= 0) {
        nextCaptureMs = now + MF35X_FAST_TRACK_INTERVAL_MS;
      }
    }

    vTaskDelay(MF35X_FAST_TRACK_POLL_TICKS);
  }
}

String mf35xFastTrackJson(const Mf35xFastTrackBatch& batch, bool replay) {
  String json = "{";
  json.reserve(1050);
  bool first = true;

  jsonULongFeld(json, first, "boot_id", batch.bootId);
  jsonULongFeld(json, first, "batch_sequence", batch.batchSequence);
  jsonULongFeld(json, first, "first_uptime_ms", batch.firstMillis);
  jsonULongFeld(json, first, "sample_interval_ms", MF35X_FAST_TRACK_INTERVAL_MS);
  jsonULongFeld(json, first, "count", batch.count);
  jsonBoolFeld(json, first, "buffered_replay", replay);
  jsonText(json, first, "point_format", "dt_ms,lat_e6,lng_e6,speed_d01_kmh,rpm,flags,hdop_centi,sats");

  if (batch.firstEpochMs > 1700000000000ULL) {
    jsonRaw(json, first, "timestamp", offlineUInt64String(batch.firstEpochMs));
    jsonText(json, first, "timestamp_source", "gps_utc");
  } else {
    jsonRaw(json, first, "timestamp", "{\".sv\":\"timestamp\"}");
    jsonText(json, first, "timestamp_source", "firebase_upload");
  }

  if (!first) json += ',';
  first = false;
  json += "\"points\":[";

  for (uint8_t i = 0; i < batch.count; ++i) {
    if (i) json += ',';
    const Mf35xFastTrackPoint& p = batch.points[i];
    json += '[';
    json += String(p.deltaMs); json += ',';
    if (p.flags & MF35X_FAST_FLAG_GPS_VALID) {
      json += String(p.latE6); json += ','; json += String(p.lngE6);
    } else {
      json += "null,null";
    }
    json += ','; json += String(p.speedDeci);
    json += ','; json += String(p.rpmValue);
    json += ','; json += String(p.flags);
    json += ','; json += String(p.hdopCenti);
    json += ','; json += String(p.satellites);
    json += ']';
  }

  json += "]}";
  return json;
}

bool mf35xFastTrackUpload(
  const String& raceId,
  const Mf35xFastTrackBatch& batch,
  bool replay
) {
  if (WiFi.status() != WL_CONNECTED || !offlineRaceIdGueltig(raceId)) return false;
  String path = "tracker/races/" + raceId + "/fastTrack/" + mf35xFastTrackBatchId(batch);
  return firebasePut(path, mf35xFastTrackJson(batch, replay));
}

bool mf35xFastTrackPersist(
  const String& raceId,
  const Mf35xFastTrackBatch& batch
) {
  if (!offlineBufferReady || !offlineRaceIdGueltig(raceId)) {
    mf35xFastTrackDroppedBatches++;
    return false;
  }

  offlineFsStatusAktualisieren();
  const size_t freeBytes = offlineFsTotalBytes > offlineFsUsedBytes
    ? offlineFsTotalBytes - offlineFsUsedBytes
    : 0;

  // Fast-Track ist Zusatzinformation und darf niemals den 5-s-Basissamples
  // oder deren Diagnose den Flash wegnehmen.
  if (freeBytes <= MF35X_FAST_TRACK_FLASH_PROTECT_BYTES + sizeof(batch)) {
    mf35xFastTrackDroppedBatches++;
    return false;
  }

  File f = LittleFS.open(mf35xFastTrackFilePath(raceId), FILE_APPEND);
  if (!f) {
    mf35xFastTrackDroppedBatches++;
    return false;
  }
  const size_t written = f.write(reinterpret_cast<const uint8_t*>(&batch), sizeof(batch));
  f.flush();
  f.close();

  if (written != sizeof(batch)) {
    mf35xFastTrackDroppedBatches++;
    return false;
  }

  mf35xFastTrackBufferedBatches++;
  offlineFsStatusAktualisieren();
  return true;
}

bool mf35xFastTrackMarkSent(const String& path, size_t offset) {
  File f = LittleFS.open(path, "r+");
  if (!f) return false;
  if (!f.seek(offset + offsetof(Mf35xFastTrackBatch, state), SeekSet)) {
    f.close();
    return false;
  }
  const uint8_t state = MF35X_FAST_TRACK_SENT;
  const bool ok = f.write(&state, 1) == 1;
  f.flush();
  f.close();
  return ok;
}

bool mf35xFastTrackFindPending(
  const String& path,
  size_t& offsetOut,
  Mf35xFastTrackBatch& batchOut
) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  size_t offset = 0;
  Mf35xFastTrackBatch b;
  while (f.available() >= (int)sizeof(b)) {
    if (f.read(reinterpret_cast<uint8_t*>(&b), sizeof(b)) != sizeof(b)) break;
    if (mf35xFastTrackBatchValid(b) && b.state != MF35X_FAST_TRACK_SENT) {
      offsetOut = offset;
      batchOut = b;
      f.close();
      return true;
    }
    offset += sizeof(b);
  }
  f.close();
  return false;
}

bool mf35xFastTrackSelectReplay() {
  File root = LittleFS.open("/");
  if (!root) return false;

  String best = "";
  File e = root.openNextFile();
  while (e) {
    String name = e.name();
    e.close();
    if (!name.startsWith("/")) name = "/" + name;
    const String raceId = mf35xFastTrackRaceIdFromPath(name);
    if (raceId.length() > 0) {
      size_t off = 0;
      Mf35xFastTrackBatch b;
      if (mf35xFastTrackFindPending(name, off, b)) {
        if (best.length() == 0 || name < best) best = name;
      }
    }
    e = root.openNextFile();
  }
  root.close();

  if (best.length() == 0) return false;
  mf35xFastTrackReplayPath = best;
  Mf35xFastTrackBatch b;
  return mf35xFastTrackFindPending(best, mf35xFastTrackReplayOffset, b);
}

void mf35xFastTrackReplayOne() {
  if (!offlineBufferReady || WiFi.status() != WL_CONNECTED) return;
  const unsigned long now = millis();
  if ((unsigned long)(now - mf35xFastTrackLastReplayMs) < MF35X_FAST_TRACK_REPLAY_MS) return;
  mf35xFastTrackLastReplayMs = now;

  if (mf35xFastTrackReplayPath.length() == 0 && !mf35xFastTrackSelectReplay()) return;

  Mf35xFastTrackBatch batch;
  size_t offset = 0;
  if (!mf35xFastTrackFindPending(mf35xFastTrackReplayPath, offset, batch)) {
    LittleFS.remove(mf35xFastTrackReplayPath);
    mf35xFastTrackReplayPath = "";
    mf35xFastTrackReplayOffset = 0;
    offlineFsStatusAktualisieren();
    return;
  }
  mf35xFastTrackReplayOffset = offset;

  const String raceId = mf35xFastTrackRaceIdFromPath(mf35xFastTrackReplayPath);
  if (raceId.length() == 0) {
    mf35xFastTrackReplayPath = "";
    return;
  }

  if (!mf35xFastTrackUpload(raceId, batch, true)) return;
  if (!mf35xFastTrackMarkSent(mf35xFastTrackReplayPath, offset)) return;

  mf35xFastTrackReplayedBatches++;
  mf35xFastTrackReplayOffset = offset + sizeof(Mf35xFastTrackBatch);
}

void mf35xFastTrackProcessRamOne() {
  if (!mf35xFastTrackQueue) return;

  Mf35xFastTrackQueueItem item = {};
  if (xQueueReceive(mf35xFastTrackQueue, &item, 0) != pdTRUE) return;

  const String raceId(item.raceId);
  if (!offlineRaceIdGueltig(raceId)) {
    mf35xFastTrackDroppedBatches++;
    return;
  }

  if (WiFi.status() == WL_CONNECTED &&
      mf35xFastTrackUpload(raceId, item.batch, false)) {
    mf35xFastTrackUploadedBatches++;
    return;
  }

  mf35xFastTrackPersist(raceId, item.batch);
}

void mf35xFastTrackDrainOne() {
  // Bereits dauerhaft gepufferte Daten zuerst langsam abbauen, gleichzeitig
  // aber pro Aufruf auch maximal einen neuen RAM-Batch bearbeiten.
  mf35xFastTrackReplayOne();
  mf35xFastTrackProcessRamOne();
}

void mf35xFastTrackSetup() {
  if (mf35xFastTrackQueue || mf35xFastTrackTaskHandle) return;

  mf35xFastTrackQueue = xQueueCreate(
    MF35X_FAST_TRACK_RAM_BATCHES,
    sizeof(Mf35xFastTrackQueueItem)
  );
  if (!mf35xFastTrackQueue) {
    Serial.println("FAST-TRACK: FEHLER - RAM-Queue konnte nicht angelegt werden");
    return;
  }

  const BaseType_t ok = xTaskCreatePinnedToCore(
    mf35xFastTrackCaptureTask,
    "mf35x_fast_track",
    MF35X_FAST_TRACK_TASK_STACK,
    nullptr,
    MF35X_FAST_TRACK_TASK_PRIORITY,
    &mf35xFastTrackTaskHandle,
    MF35X_FAST_TRACK_TASK_CORE
  );

  if (ok != pdPASS) {
    vQueueDelete(mf35xFastTrackQueue);
    mf35xFastTrackQueue = nullptr;
    mf35xFastTrackTaskHandle = nullptr;
    Serial.println("FAST-TRACK: FEHLER - Capture-Task konnte nicht gestartet werden");
    return;
  }

  Serial.println(
    "FAST-TRACK: 10-Hz GPS/RPM/GPIO11-Track aktiv - vorbereitet fuer 50-Hz-GPS"
  );
}
