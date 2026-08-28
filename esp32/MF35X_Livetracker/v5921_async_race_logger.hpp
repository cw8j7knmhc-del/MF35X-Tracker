#pragma once

#include <esp32-hal-psram.h>
#include <string.h>

// ==================================================
// MF35X V5.9.21 - NETZUNABHAENGIGER RENNLOGGER
// ==================================================
// Ziel:
// - Rennsamples werden in einem eigenen FreeRTOS-Task nach monotonic millis()
//   erzeugt und koennen daher nicht mehr von Firebase/HTTP/LTE blockiert werden.
// - Der Capture-Task macht KEINEN HTTP-Aufruf und KEINEN LittleFS-Zugriff.
// - Basissample + Oeldruckdiagnose + RPM/GPIO11-Diagnose werden gemeinsam in
//   einem RAM/PSRAM-Ring abgelegt, damit alle drei exakt dieselbe sequence haben.
// - Die normale loop() uebernimmt die Ringdaten danach in die bereits
//   bewaehrten LittleFS-Queues. Erst von dort erfolgt der Netzversand.
// - Die alte synchrone rennhistorieBearbeiten()-Erzeugung bleibt als Fallback
//   erhalten, wird bei laufendem Async-Task aber durch regelmaessiges Aktualisieren
//   von letzterHistoryUpload unterdrueckt.
// ==================================================

constexpr size_t MF35X_ASYNC_RACE_ID_MAX = 48;
constexpr size_t MF35X_ASYNC_RING_PSRAM_RECORDS = 512;
constexpr size_t MF35X_ASYNC_RING_FALLBACK_RECORDS = 64;
constexpr uint32_t MF35X_ASYNC_CAPTURE_TASK_STACK = 6144;
constexpr UBaseType_t MF35X_ASYNC_CAPTURE_TASK_PRIORITY = 2;
constexpr BaseType_t MF35X_ASYNC_CAPTURE_TASK_CORE = 0;
constexpr TickType_t MF35X_ASYNC_CAPTURE_TASK_TICK = pdMS_TO_TICKS(10);
constexpr unsigned long MF35X_ASYNC_STATUS_MS = 30000UL;
constexpr unsigned long MF35X_ASYNC_STATS_UPLOAD_MS = 10000UL;

struct Mf35xAsyncRaceConfig {
  bool enabled;
  uint32_t intervalMs;
  char raceId[MF35X_ASYNC_RACE_ID_MAX + 1];
  uint32_t generation;
};

struct Mf35xAsyncRaceEnvelope {
  OfflineRaceRecord base;
  Mf35xOilDiagRecord oilDiag;
  Mf35xRpmDiagRecord rpmDiag;
  char raceId[MF35X_ASYNC_RACE_ID_MAX + 1];
};

portMUX_TYPE mf35xAsyncConfigMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE mf35xAsyncRingMux = portMUX_INITIALIZER_UNLOCKED;

Mf35xAsyncRaceConfig mf35xAsyncConfig = {};
Mf35xAsyncRaceEnvelope mf35xAsyncFallbackRing[MF35X_ASYNC_RING_FALLBACK_RECORDS];
Mf35xAsyncRaceEnvelope* mf35xAsyncRing = mf35xAsyncFallbackRing;
size_t mf35xAsyncRingCapacity = MF35X_ASYNC_RING_FALLBACK_RECORDS;
size_t mf35xAsyncRingHead = 0;
size_t mf35xAsyncRingTail = 0;
size_t mf35xAsyncRingCount = 0;
size_t mf35xAsyncRingHighWater = 0;

TaskHandle_t mf35xAsyncCaptureTaskHandle = nullptr;
volatile uint32_t mf35xAsyncCaptured = 0;
volatile uint32_t mf35xAsyncPersisted = 0;
volatile uint32_t mf35xAsyncRingDropped = 0;
volatile uint32_t mf35xAsyncPersistFailed = 0;
volatile uint32_t mf35xAsyncScheduleSkipped = 0;
volatile uint32_t mf35xAsyncLastCaptureMs = 0;
volatile uint32_t mf35xAsyncLastGapMs = 0;
volatile uint32_t mf35xAsyncMaxGapMs = 0;
volatile uint32_t mf35xAsyncLastPersistedSequence = 0;

unsigned long mf35xAsyncLastStatusMs = 0;
unsigned long mf35xAsyncLastStatsUploadMs = 0;
bool mf35xAsyncStatsDirty = false;

bool mf35xAsyncRaceLoggerActive() {
  return mf35xAsyncCaptureTaskHandle != nullptr;
}

void mf35xAsyncRaceLoggerSyncConfig() {
  bool enabled = recordingConfig.enabled && recordingConfig.raceId.length() > 0;
  uint32_t intervalMs = recordingConfig.historyUpdateMs;
  if (intervalMs < 1000UL || intervalMs > 60000UL) {
    intervalMs = intervalConfig.historyUpdateMs;
  }
  if (intervalMs < 1000UL || intervalMs > 60000UL) {
    intervalMs = 5000UL;
  }

  char raceId[MF35X_ASYNC_RACE_ID_MAX + 1] = {};
  if (enabled) {
    recordingConfig.raceId.toCharArray(raceId, sizeof(raceId));
    if (!offlineRaceIdGueltig(String(raceId))) {
      enabled = false;
      raceId[0] = '\0';
    }
  }

  portENTER_CRITICAL(&mf35xAsyncConfigMux);
  const bool changed =
    mf35xAsyncConfig.enabled != enabled ||
    mf35xAsyncConfig.intervalMs != intervalMs ||
    strncmp(mf35xAsyncConfig.raceId, raceId, sizeof(mf35xAsyncConfig.raceId)) != 0;

  if (changed) {
    mf35xAsyncConfig.enabled = enabled;
    mf35xAsyncConfig.intervalMs = intervalMs;
    memcpy(mf35xAsyncConfig.raceId, raceId, sizeof(mf35xAsyncConfig.raceId));
    mf35xAsyncConfig.generation++;
    if (mf35xAsyncConfig.generation == 0) mf35xAsyncConfig.generation = 1;
  }
  portEXIT_CRITICAL(&mf35xAsyncConfigMux);
}

Mf35xAsyncRaceConfig mf35xAsyncConfigLesen() {
  Mf35xAsyncRaceConfig cfg;
  portENTER_CRITICAL(&mf35xAsyncConfigMux);
  cfg = mf35xAsyncConfig;
  portEXIT_CRITICAL(&mf35xAsyncConfigMux);
  return cfg;
}

bool mf35xAsyncRingPush(const Mf35xAsyncRaceEnvelope& env) {
  bool ok = false;
  portENTER_CRITICAL(&mf35xAsyncRingMux);
  if (mf35xAsyncRingCount < mf35xAsyncRingCapacity) {
    mf35xAsyncRing[mf35xAsyncRingHead] = env;
    mf35xAsyncRingHead = (mf35xAsyncRingHead + 1U) % mf35xAsyncRingCapacity;
    mf35xAsyncRingCount++;
    if (mf35xAsyncRingCount > mf35xAsyncRingHighWater) {
      mf35xAsyncRingHighWater = mf35xAsyncRingCount;
    }
    ok = true;
  }
  portEXIT_CRITICAL(&mf35xAsyncRingMux);
  return ok;
}

bool mf35xAsyncRingPop(Mf35xAsyncRaceEnvelope& env) {
  bool ok = false;
  portENTER_CRITICAL(&mf35xAsyncRingMux);
  if (mf35xAsyncRingCount > 0) {
    env = mf35xAsyncRing[mf35xAsyncRingTail];
    mf35xAsyncRingTail = (mf35xAsyncRingTail + 1U) % mf35xAsyncRingCapacity;
    mf35xAsyncRingCount--;
    ok = true;
  }
  portEXIT_CRITICAL(&mf35xAsyncRingMux);
  return ok;
}

size_t mf35xAsyncRingCountLesen() {
  size_t count;
  portENTER_CRITICAL(&mf35xAsyncRingMux);
  count = mf35xAsyncRingCount;
  portEXIT_CRITICAL(&mf35xAsyncRingMux);
  return count;
}

Mf35xOilDiagRecord mf35xAsyncOilDiagBauen(uint32_t sequence) {
  Mf35xOilDiagRecord rec = {};
  rec.magic = MF35X_DIAG_MAGIC;
  rec.version = MF35X_DIAG_VERSION;
  rec.size = sizeof(rec);
  rec.bootId = offlineBootId;
  rec.sequence = sequence;
  rec.rawAdc = mf35xDiagRawAdc();
  rec.diagState = mf35xDiagStateNow();
  rec.gpio11 = schaltausgangAktiv ? 1U : 0U;
  rec.state = MF35X_DIAG_PENDING;
  rec.crc32 = mf35xDiagRecordCrc(rec);
  return rec;
}

void mf35xAsyncCaptureTask(void*) {
  uint32_t seenGeneration = 0;
  uint32_t nextDueMs = 0;

  for (;;) {
    const uint32_t now = millis();

    // Unterdrueckt ausschliesslich den alten synchronen Basissample-Erzeuger.
    // Netzwerkversand/Offline-Drain im Core bleibt unveraendert aktiv.
    letzterHistoryUpload = now;

    const Mf35xAsyncRaceConfig cfg = mf35xAsyncConfigLesen();

    if (!cfg.enabled || cfg.raceId[0] == '\0') {
      nextDueMs = 0;
      seenGeneration = cfg.generation;
      vTaskDelay(MF35X_ASYNC_CAPTURE_TASK_TICK);
      continue;
    }

    if (cfg.generation != seenGeneration || nextDueMs == 0) {
      // Bei Rennstart, Rennwechsel oder Intervallaenderung sofort einen
      // definierten ersten Messpunkt erzeugen; danach streng im Raster weiter.
      seenGeneration = cfg.generation;
      nextDueMs = now;
    }

    if ((int32_t)(now - nextDueMs) >= 0) {
      Mf35xAsyncRaceEnvelope env = {};
      env.base = offlineRecordBauen();
      env.oilDiag = mf35xAsyncOilDiagBauen(env.base.sequence);
      env.rpmDiag = mf35xRpmDiagBuild(env.base.sequence);
      memcpy(env.raceId, cfg.raceId, sizeof(env.raceId));

      const uint32_t capturedMs = env.base.capturedMillis;
      const uint32_t previousCaptureMs = mf35xAsyncLastCaptureMs;
      if (previousCaptureMs != 0) {
        const uint32_t gap = capturedMs - previousCaptureMs;
        mf35xAsyncLastGapMs = gap;
        if (gap > mf35xAsyncMaxGapMs) mf35xAsyncMaxGapMs = gap;
      }
      mf35xAsyncLastCaptureMs = capturedMs;

      if (mf35xAsyncRingPush(env)) {
        mf35xAsyncCaptured++;
      } else {
        mf35xAsyncRingDropped++;
      }

      // Kein driftendes "jetzt + Intervall": Das Sollraster wird fortgefuehrt.
      nextDueMs += cfg.intervalMs;
      if ((int32_t)(now - nextDueMs) >= 0) {
        const uint32_t behind = now - nextDueMs;
        const uint32_t skipped = behind / cfg.intervalMs + 1UL;
        mf35xAsyncScheduleSkipped += skipped;
        nextDueMs += skipped * cfg.intervalMs;
      }
    }

    vTaskDelay(MF35X_ASYNC_CAPTURE_TASK_TICK);
  }
}

void mf35xAsyncRaceLoggerFlush(size_t maxRecords = 0) {
  if (!mf35xAsyncRaceLoggerActive()) return;

  size_t done = 0;
  Mf35xAsyncRaceEnvelope env;

  while ((maxRecords == 0 || done < maxRecords) && mf35xAsyncRingPop(env)) {
    const String raceId(env.raceId);

    if (!offlineRecordDauerhaftPuffern(raceId, env.base)) {
      mf35xAsyncPersistFailed++;
      historyFehler++;
      done++;
      continue;
    }

    mf35xAsyncPersisted++;
    mf35xAsyncLastPersistedSequence = env.base.sequence;

    // Diagnosewerte gehoeren exakt zu diesem Basissample. Diagnosequeues duerfen
    // bei knappem Flash gemaess bestehender Schutzlogik frueher verwerfen; der
    // 64-Byte-Basissample hat immer Vorrang.
    mf35xDiagQueueAppend(raceId, env.oilDiag);
    mf35xRpmDiagQueueAppend(raceId, env.rpmDiag);

    if (mf35xDiagRaceId != raceId) {
      mf35xDiagStatsReset(raceId);
    }
    if ((env.base.flags & OFFLINE_FLAG_OIL_PRESSURE_VALID) != 0) {
      mf35xDiagStatsAdd((float)env.base.oilPressureCenti / 100.0f);
      mf35xAsyncStatsDirty = true;
    }

    done++;
  }
}

void mf35xAsyncRaceLoggerMaintenance() {
  if (!mf35xAsyncRaceLoggerActive()) return;

  // Funktionen aus dem bisherigen V5.9.17/V5.9.20-Pfad, jedoch ohne dessen
  // sequence-basierte Capture-Logik. Capture findet ausschliesslich im Task statt.
  mf35xMaxPlausibilisieren();

  if (!recordingConfig.enabled || recordingConfig.raceId.length() == 0) {
    mf35xDiagStatsPersist(false);
  }

  // Netzwerk darf hier blockieren: Der Capture-Task laeuft davon unabhaengig weiter.
  mf35xDiagDrainOne();
  mf35xRpmDiagDrainOne();

  const unsigned long now = millis();
  if (mf35xAsyncStatsDirty && WiFi.status() == WL_CONNECTED &&
      (unsigned long)(now - mf35xAsyncLastStatsUploadMs) >= MF35X_ASYNC_STATS_UPLOAD_MS) {
    mf35xAsyncLastStatsUploadMs = now;
    mf35xDiagStatsUpload();
    mf35xAsyncStatsDirty = false;
  }

  if ((unsigned long)(now - mf35xAsyncLastStatusMs) >= MF35X_ASYNC_STATUS_MS) {
    mf35xAsyncLastStatusMs = now;
    Serial.print("ASYNC-RENNLOGGER: Task=OK | Ring ");
    Serial.print((unsigned long)mf35xAsyncRingCountLesen());
    Serial.print('/');
    Serial.print((unsigned long)mf35xAsyncRingCapacity);
    Serial.print(" | HighWater ");
    Serial.print((unsigned long)mf35xAsyncRingHighWater);
    Serial.print(" | Capture/Persist ");
    Serial.print(mf35xAsyncCaptured);
    Serial.print('/');
    Serial.print(mf35xAsyncPersisted);
    Serial.print(" | Drop/PersistFail ");
    Serial.print(mf35xAsyncRingDropped);
    Serial.print('/');
    Serial.print(mf35xAsyncPersistFailed);
    Serial.print(" | Gap last/max ");
    Serial.print(mf35xAsyncLastGapMs);
    Serial.print('/');
    Serial.print(mf35xAsyncMaxGapMs);
    Serial.print(" ms | ScheduleSkip ");
    Serial.println(mf35xAsyncScheduleSkipped);
  }
}

void mf35xAsyncRaceLoggerSetup() {
  // Aktuelle NVS/Firebase-Konfiguration in eine feste, task-sichere Struktur kopieren.
  mf35xAsyncRaceLoggerSyncConfig();

  // Auf dem bestaetigten ESP32-S3 8MB/8MB wird der grosse Ring in PSRAM gelegt.
  // Fallback bleibt ein kleiner interner Ring, damit der Sketch auch ohne PSRAM bootet.
  if (psramFound()) {
    void* mem = ps_malloc(sizeof(Mf35xAsyncRaceEnvelope) * MF35X_ASYNC_RING_PSRAM_RECORDS);
    if (mem != nullptr) {
      mf35xAsyncRing = static_cast<Mf35xAsyncRaceEnvelope*>(mem);
      mf35xAsyncRingCapacity = MF35X_ASYNC_RING_PSRAM_RECORDS;
      memset(mf35xAsyncRing, 0, sizeof(Mf35xAsyncRaceEnvelope) * mf35xAsyncRingCapacity);
    }
  }

  letzterHistoryUpload = millis();

  BaseType_t ok = xTaskCreatePinnedToCore(
    mf35xAsyncCaptureTask,
    "mf35x-race-capture",
    MF35X_ASYNC_CAPTURE_TASK_STACK,
    nullptr,
    MF35X_ASYNC_CAPTURE_TASK_PRIORITY,
    &mf35xAsyncCaptureTaskHandle,
    MF35X_ASYNC_CAPTURE_TASK_CORE
  );

  if (ok != pdPASS) {
    mf35xAsyncCaptureTaskHandle = nullptr;
    Serial.println("ASYNC-RENNLOGGER: Taskstart FEHLER - alter synchroner Logger bleibt als Fallback aktiv.");
    return;
  }

  Serial.print("ASYNC-RENNLOGGER: AKTIV | Capture ohne HTTP/LittleFS | Ring ");
  Serial.print((unsigned long)mf35xAsyncRingCapacity);
  Serial.print(" Records | Speicher: ");
  Serial.println(mf35xAsyncRing == mf35xAsyncFallbackRing ? "intern" : "PSRAM");
}
