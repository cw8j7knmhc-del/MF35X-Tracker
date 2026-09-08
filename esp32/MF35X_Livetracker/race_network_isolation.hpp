#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ==================================================
// NAECHSTER USB-STAND - PUNKT 1B + 2 + 5 + 12
// RENNNETZWERK ENTKOPPELT + ATOMARE DATEN + FAST-TRACK + NETZDIAGNOSE
// ==================================================
// - normale Arduino-loop() macht fuer Rennsamples keinen HTTP-Aufruf
// - Capture der Voll-Telemetrie erfolgt atomar in race_timing_fix.hpp
// - Basisrecord + Oeldruckdiagnose + RPM/GPIO11-Diagnose erhalten exakt
//   dieselbe sequence
// - der zuletzt unabhaengig gemessene Netzwerkstatus wird mit dem Capture-
//   Zeitpunkt verknuepft und als weiterer Companion-Datensatz gespeichert
// - fast_track_logger.hpp zeichnet parallel den Strecken-/Fahrzustand mit
//   10 Hz auf und fasst die Punkte zu ~1-s-Paketen zusammen
// - erst dieser Background-Task sendet Renn- und Fast-Track-Daten zu Firebase
// ==================================================

constexpr uint32_t MF35X_RACE_UPLOAD_TASK_STACK = 6144;
constexpr UBaseType_t MF35X_RACE_UPLOAD_TASK_PRIORITY = 1;
constexpr BaseType_t MF35X_RACE_UPLOAD_TASK_CORE = 0;
constexpr TickType_t MF35X_RACE_UPLOAD_TASK_DELAY = pdMS_TO_TICKS(25);
constexpr unsigned long MF35X_RACE_STATS_UPLOAD_MS = 10000UL;

TaskHandle_t mf35xRaceUploadTaskHandle = nullptr;
volatile uint32_t mf35xRacePersisted = 0;
volatile uint32_t mf35xRacePersistErrors = 0;
volatile uint32_t mf35xRaceBackgroundLoops = 0;
unsigned long mf35xRaceLastStatsUploadMs = 0;

void mf35xRacePersistOne() {
  mf35xRaceTimingConfigSync();

  // Nur Notfall-Fallback. Im Normalbetrieb ist der eigene Timing-Task aktiv.
  if (!mf35xRaceTimingQueue || !mf35xRaceTimingTaskHandle) {
    rennhistorieBearbeiten();
    return;
  }

  Mf35xTimedRaceCapture item = {};
  if (xQueueReceive(mf35xRaceTimingQueue, &item, 0) != pdTRUE) return;

  const String raceId(item.raceId);
  if (!offlineRaceIdGueltig(raceId)) {
    historyFehler++;
    mf35xRacePersistErrors++;
    mf35xRaceTimingProcessed++;
    return;
  }

  // Netzwerkstatus wird nicht neu aktiv gemessen. Es wird nur der letzte
  // bereits vom eigenen Diagnose-Task eingefrorene Snapshot uebernommen und
  // dessen Alter relativ zum echten Capture-Zeitpunkt dokumentiert.
  Mf35xNetDiagRecord netDiag =
    mf35xConnectivityRaceRecordBauen(item.rec.capturedMillis);

  // EIN gemeinsamer Sample-Key fuer Basis + alle Companion-Diagnosen.
  const uint32_t sequence = ++offlineSampleSequence;
  item.rec.sequence = sequence;
  item.oilDiag.sequence = sequence;
  item.rpmDiag.sequence = sequence;
  netDiag.sequence = sequence;

  item.rec.bootId = offlineBootId;
  item.oilDiag.bootId = offlineBootId;
  item.rpmDiag.bootId = offlineBootId;
  netDiag.bootId = offlineBootId;

  item.rec.crc32 = offlineRecordCrc(item.rec);
  item.oilDiag.crc32 = mf35xDiagRecordCrc(item.oilDiag);
  item.rpmDiag.crc32 = mf35xRpmDiagRecordCrc(item.rpmDiag);
  netDiag.crc32 = mf35xNetDiagCrc(netDiag);

  // Basisdaten haben immer Prioritaet. Nur wenn der Basissample sicher in
  // LittleFS liegt, werden die Companion-Diagnosen angehaengt.
  if (!offlineRecordDauerhaftPuffern(raceId, item.rec)) {
    historyFehler++;
    mf35xRacePersistErrors++;
    mf35xRaceTimingProcessed++;
    return;
  }

  mf35xRacePersisted++;
  mf35xRaceTimingProcessed++;

  // Statistik verwendet exakt den beim atomaren Capture eingefrorenen Wert.
  if (isfinite(item.oilPressureBarForStats)) {
    if (mf35xDiagRaceId != raceId) {
      mf35xDiagStatsReset(raceId);
    }
    mf35xDiagStatsAdd(item.oilPressureBarForStats);
  }

  // KEIN erneutes Lesen von rpm/oilPressure/gpio11 an dieser Stelle.
  // Die Records stammen unveraendert vom gemeinsamen Capture-Zeitpunkt.
  mf35xDiagQueueAppend(raceId, item.oilDiag);
  mf35xRpmDiagQueueAppend(raceId, item.rpmDiag);
  mf35xNetDiagQueueAppend(raceId, netDiag);
}

void mf35xRaceUploadTask(void*) {
  for (;;) {
    mf35xRaceBackgroundLoops++;

    if (WiFi.status() == WL_CONNECTED) {
      // Wichtigste Daten zuerst: 5-s-Basissample und dessen Diagnosen.
      offlineDrainBearbeiten();
      mf35xDiagDrainOne();
      mf35xRpmDiagDrainOne();
      mf35xNetDiagDrainOne();

      const unsigned long now = millis();
      if (mf35xDiagRaceId.length() > 0 && mf35xDiagCount > 0 &&
          (unsigned long)(now - mf35xRaceLastStatsUploadMs) >=
            MF35X_RACE_STATS_UPLOAD_MS) {
        mf35xRaceLastStatsUploadMs = now;
        mf35xDiagStatsUpload();
      }
    }

    // Punkt 5: Fast-Track immer bearbeiten. Online wird hochgeladen; offline
    // wird der RAM-Batch mit niedriger Prioritaet in LittleFS gepuffert.
    // Der Fast-Track darf den Basis-Rennpuffer nie verdraengen.
    mf35xFastTrackDrainOne();

    vTaskDelay(MF35X_RACE_UPLOAD_TASK_DELAY);
  }
}

void mf35xRaceNetworkIsolationSetup() {
  if (mf35xRaceUploadTaskHandle) return;

  const BaseType_t ok = xTaskCreatePinnedToCore(
    mf35xRaceUploadTask,
    "mf35x_race_upload",
    MF35X_RACE_UPLOAD_TASK_STACK,
    nullptr,
    MF35X_RACE_UPLOAD_TASK_PRIORITY,
    &mf35xRaceUploadTaskHandle,
    MF35X_RACE_UPLOAD_TASK_CORE
  );

  if (ok != pdPASS) {
    mf35xRaceUploadTaskHandle = nullptr;
    Serial.println("RACE-UPLOAD: FEHLER - Background-Task konnte nicht gestartet werden");
    return;
  }

  Serial.println(
    "RACE-UPLOAD: Netzwerk entkoppelt; atomare Rennsamples + Netzdiagnose + 10-Hz-Fast-Track aktiv"
  );
}

void mf35xRaceNoNetworkHousekeeping() {
  // Nur lokale/Plausibilitaetsarbeit. Keine rennbezogenen HTTP-Zugriffe.
  mf35xMaxPlausibilisieren();

  if (!recordingConfig.enabled || recordingConfig.raceId.length() == 0) {
    mf35xDiagStatsPersist(false);
  }
}
