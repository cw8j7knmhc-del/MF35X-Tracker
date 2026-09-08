#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <string.h>

// ==================================================
// NAECHSTER USB-STAND - PUNKT 1
// DETERMINISTISCHE RENNAUFZEICHNUNG
// ==================================================
// Problem im bisherigen Stand:
// rennhistorieBearbeiten() lief in derselben loop() wie HTTPS/Firebase.
// Mehrere blockierende HTTP-Aufrufe konnten deshalb aus eingestellten 5 s
// reale Luecken von deutlich ueber 5 s machen.
//
// Loesung:
// - Ein eigener FreeRTOS-Task erzeugt den Renn-Snapshot zeitlich unabhaengig
//   von WLAN/Firebase.
// - Der Task macht KEINE HTTP- und KEINE Flash-Zugriffe. Er legt nur einen
//   kompakten Snapshot in eine RAM-Queue.
// - Die normale loop() verarbeitet pro Durchlauf genau einen Snapshot. Erst
//   dort wird die Sample-Sequenz vergeben und wie bisher direkt gesendet oder
//   bei Fehler/Offline dauerhaft in LittleFS gepuffert.
// - Damit bleiben die vorhandene Offline-Queue, deterministische Sample-IDs
//   und die Diagnose-Patches kompatibel.
//
// Hinweis fuer Punkt 10:
// Ein Snapshot, der waehrend eines blockierenden HTTP-Aufrufs nur in der
// RAM-Queue wartet, ist bis zur Verarbeitung noch nicht stromausfallsicher.
// Das wird beim geplanten realen Offline-/Power-Cycle-Test gezielt geprueft.
// ==================================================

constexpr uint8_t MF35X_RACE_TIMING_QUEUE_LEN = 32;
constexpr uint32_t MF35X_RACE_TIMING_TASK_STACK = 4096;
constexpr UBaseType_t MF35X_RACE_TIMING_TASK_PRIORITY = 2;
constexpr BaseType_t MF35X_RACE_TIMING_TASK_CORE = 1;
constexpr TickType_t MF35X_RACE_TIMING_POLL_TICKS = pdMS_TO_TICKS(5);
constexpr size_t MF35X_RACE_ID_BUFFER_LEN = 49; // max. 48 Zeichen + \0

struct Mf35xRaceTimingConfig {
  bool enabled;
  uint32_t intervalMs;
  uint32_t revision;
  char raceId[MF35X_RACE_ID_BUFFER_LEN];
};

struct Mf35xTimedRaceCapture {
  OfflineRaceRecord rec;
  char raceId[MF35X_RACE_ID_BUFFER_LEN];
};

QueueHandle_t mf35xRaceTimingQueue = nullptr;
TaskHandle_t mf35xRaceTimingTaskHandle = nullptr;
portMUX_TYPE mf35xRaceTimingConfigMux = portMUX_INITIALIZER_UNLOCKED;
Mf35xRaceTimingConfig mf35xRaceTimingConfig = {};

volatile uint32_t mf35xRaceTimingCaptured = 0;
volatile uint32_t mf35xRaceTimingProcessed = 0;
volatile uint32_t mf35xRaceTimingQueueDropped = 0;
volatile uint32_t mf35xRaceTimingScheduleMissed = 0;
volatile uint32_t mf35xRaceTimingLastDeltaMs = 0;
volatile uint32_t mf35xRaceTimingMaxJitterMs = 0;

uint32_t mf35xRaceTimingIntervall() {
  uint32_t intervall = (uint32_t)recordingConfig.historyUpdateMs;
  if (intervall < 1000UL || intervall > 60000UL) {
    intervall = (uint32_t)intervalConfig.historyUpdateMs;
  }
  if (intervall < 1000UL || intervall > 60000UL) {
    intervall = 5000UL;
  }
  return intervall;
}

void mf35xRaceTimingConfigSync() {
  const bool enabled =
    recordingConfig.enabled &&
    recordingConfig.raceId.length() > 0 &&
    offlineRaceIdGueltig(recordingConfig.raceId);

  const uint32_t intervall = mf35xRaceTimingIntervall();
  char raceId[MF35X_RACE_ID_BUFFER_LEN] = {};
  if (enabled) {
    strncpy(raceId, recordingConfig.raceId.c_str(), sizeof(raceId) - 1);
  }

  portENTER_CRITICAL(&mf35xRaceTimingConfigMux);
  const bool changed =
    mf35xRaceTimingConfig.enabled != enabled ||
    mf35xRaceTimingConfig.intervalMs != intervall ||
    strncmp(mf35xRaceTimingConfig.raceId, raceId, sizeof(raceId)) != 0;

  if (changed) {
    mf35xRaceTimingConfig.enabled = enabled;
    mf35xRaceTimingConfig.intervalMs = intervall;
    memcpy(mf35xRaceTimingConfig.raceId, raceId, sizeof(raceId));
    mf35xRaceTimingConfig.revision++;
    if (mf35xRaceTimingConfig.revision == 0) {
      mf35xRaceTimingConfig.revision = 1;
    }
  }
  portEXIT_CRITICAL(&mf35xRaceTimingConfigMux);
}

Mf35xRaceTimingConfig mf35xRaceTimingConfigLesen() {
  Mf35xRaceTimingConfig copy = {};
  portENTER_CRITICAL(&mf35xRaceTimingConfigMux);
  copy = mf35xRaceTimingConfig;
  portEXIT_CRITICAL(&mf35xRaceTimingConfigMux);
  return copy;
}

OfflineRaceRecord mf35xRaceCaptureRecordBauen() {
  // Inhalt bewusst analog zu offlineRecordBauen(), aber OHNE Vergabe der
  // Sample-Sequenz. Diese wird erst beim Verarbeiten in der normalen loop()
  // vergeben. Dadurch sehen die vorhandenen Diagnose-Patches weiterhin pro
  // verarbeitetem Basissample exakt einen Sequenzsprung.
  OfflineRaceRecord rec = {};
  rec.magic = OFFLINE_RECORD_MAGIC;
  rec.version = OFFLINE_RECORD_VERSION;
  rec.size = sizeof(OfflineRaceRecord);
  rec.bootId = offlineBootId;
  rec.sequence = 0;
  rec.capturedMillis = millis();
  rec.state = OFFLINE_STATE_PENDING;

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

  rec.wifiRssi =
    WiFi.status() == WL_CONNECTED ? (int16_t)WiFi.RSSI() : (int16_t)-127;

  // CRC wird nach Vergabe der finalen Sequenz in der loop() berechnet.
  rec.crc32 = 0;
  return rec;
}

void mf35xRaceTimingTask(void*) {
  uint32_t lastRevision = 0;
  uint32_t nextCaptureMs = 0;
  uint32_t lastCaptureMs = 0;

  for (;;) {
    const Mf35xRaceTimingConfig cfg = mf35xRaceTimingConfigLesen();
    const uint32_t now = millis();

    if (cfg.revision != lastRevision) {
      lastRevision = cfg.revision;
      nextCaptureMs = cfg.enabled ? now : 0;
      lastCaptureMs = 0;
    }

    if (cfg.enabled && cfg.intervalMs >= 1000UL && nextCaptureMs != 0 &&
        (int32_t)(now - nextCaptureMs) >= 0) {
      Mf35xTimedRaceCapture item = {};
      item.rec = mf35xRaceCaptureRecordBauen();
      strncpy(item.raceId, cfg.raceId, sizeof(item.raceId) - 1);

      if (lastCaptureMs != 0) {
        const uint32_t delta =
          (uint32_t)(item.rec.capturedMillis - lastCaptureMs);
        mf35xRaceTimingLastDeltaMs = delta;
        const uint32_t jitter =
          delta > cfg.intervalMs ? delta - cfg.intervalMs : cfg.intervalMs - delta;
        if (jitter > mf35xRaceTimingMaxJitterMs) {
          mf35xRaceTimingMaxJitterMs = jitter;
        }
      }
      lastCaptureMs = item.rec.capturedMillis;

      if (mf35xRaceTimingQueue &&
          xQueueSend(mf35xRaceTimingQueue, &item, 0) == pdTRUE) {
        mf35xRaceTimingCaptured++;
      } else {
        mf35xRaceTimingQueueDropped++;
      }

      nextCaptureMs += cfg.intervalMs;

      // Falls der Scheduler selbst jemals um mindestens ein ganzes Intervall
      // verspaetet wurde, keine kuenstlichen Mehrfach-Samples mit identischen
      // Istwerten erzeugen. Stattdessen Zaehler erhoehen und sauber neu takten.
      if ((int32_t)(now - nextCaptureMs) >= 0) {
        const uint32_t missed =
          ((uint32_t)(now - nextCaptureMs) / cfg.intervalMs) + 1UL;
        mf35xRaceTimingScheduleMissed += missed;
        nextCaptureMs = now + cfg.intervalMs;
      }
    }

    vTaskDelay(MF35X_RACE_TIMING_POLL_TICKS);
  }
}

void mf35xRaceTimingSetup() {
  if (mf35xRaceTimingQueue || mf35xRaceTimingTaskHandle) return;

  mf35xRaceTimingQueue = xQueueCreate(
    MF35X_RACE_TIMING_QUEUE_LEN,
    sizeof(Mf35xTimedRaceCapture)
  );

  if (!mf35xRaceTimingQueue) {
    Serial.println("RACE-TIMING: FEHLER - RAM-Queue konnte nicht angelegt werden");
    return;
  }

  const BaseType_t ok = xTaskCreatePinnedToCore(
    mf35xRaceTimingTask,
    "mf35x_race_capture",
    MF35X_RACE_TIMING_TASK_STACK,
    nullptr,
    MF35X_RACE_TIMING_TASK_PRIORITY,
    &mf35xRaceTimingTaskHandle,
    MF35X_RACE_TIMING_TASK_CORE
  );

  if (ok != pdPASS) {
    vQueueDelete(mf35xRaceTimingQueue);
    mf35xRaceTimingQueue = nullptr;
    mf35xRaceTimingTaskHandle = nullptr;
    Serial.println("RACE-TIMING: FEHLER - Capture-Task konnte nicht gestartet werden");
    return;
  }

  Serial.println(
    "RACE-TIMING: eigener Capture-Task aktiv - Rennintervall von HTTPS/Firebase entkoppelt"
  );
}

void mf35xRaceTimingBearbeiten() {
  // Die Konfiguration wird nur in der normalen loop() aus den String-basierten
  // Recording-Werten in einen task-sicheren Fixpuffer gespiegelt.
  mf35xRaceTimingConfigSync();

  // Sicherer Fallback: Sollte der Task/Queue-Start wider Erwarten fehlschlagen,
  // bleibt die bisherige Rennaufzeichnung funktionsfaehig.
  if (!mf35xRaceTimingQueue || !mf35xRaceTimingTaskHandle) {
    rennhistorieBearbeiten();
    return;
  }

  Mf35xTimedRaceCapture item = {};
  if (xQueueReceive(mf35xRaceTimingQueue, &item, 0) != pdTRUE) {
    return;
  }

  OfflineRaceRecord rec = item.rec;
  rec.sequence = ++offlineSampleSequence;
  rec.crc32 = offlineRecordCrc(rec);
  const String raceId(item.raceId);

  // Ab hier exakt das bewaehrte V5.9.19-Verhalten:
  // online ohne Rueckstau direkt PUT, andernfalls dauerhaft in LittleFS.
  if (WiFi.status() == WL_CONNECTED && offlinePendingCount == 0) {
    if (offlineRecordSenden(raceId, rec, false)) {
      historyOk++;
      mf35xRaceTimingProcessed++;
      return;
    }
    historyFehler++;
  }

  if (!offlineRecordDauerhaftPuffern(raceId, rec)) {
    historyFehler++;
  }
  mf35xRaceTimingProcessed++;
}

uint32_t mf35xRaceTimingQueuePending() {
  return mf35xRaceTimingQueue
    ? (uint32_t)uxQueueMessagesWaiting(mf35xRaceTimingQueue)
    : 0UL;
}
