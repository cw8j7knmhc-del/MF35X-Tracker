#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ==================================================
// NAECHSTER USB-STAND - PUNKT 1B
// RENNAUFZEICHNUNG DARF LIVE-BETRIEB NICHT BLOCKIEREN
// ==================================================
// Ziel:
// - Capture bleibt im eigenen Race-Timing-Task.
// - Die normale Arduino-loop() macht fuer Rennsamples KEINEN HTTP-Aufruf.
// - Ein verarbeiteter Rennsample wird zuerst dauerhaft in LittleFS abgelegt.
// - Oeldruck-/RPM-Diagnose wird ebenfalls nur lokal gepuffert.
// - Ein eigener Background-Task sendet Basisdaten und Diagnose zu Firebase.
//
// Damit koennen Firebase-/LTE-Timeouts die Live-Website nicht mehr direkt
// durch rennbezogene PUT/PATCH/GET-Aufrufe blockieren.
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

void mf35xRaceQueueOilDiagLocal(const String& raceId, uint32_t sequence) {
  if (!offlineBufferReady || !offlineRaceIdGueltig(raceId) || sequence == 0) return;

  if (mf35xDiagRaceId != raceId) {
    mf35xDiagStatsReset(raceId);
  }

  // Statistik bleibt lokal; der Netzwerk-Upload erfolgt ausschliesslich im
  // Background-Task.
  mf35xDiagStatsAdd(oilPressureBar);

  Mf35xOilDiagRecord rec = {};
  rec.magic = MF35X_DIAG_MAGIC;
  rec.version = MF35X_DIAG_VERSION;
  rec.size = sizeof(rec);
  rec.bootId = offlineBootId;
  rec.sequence = sequence;
  rec.rawAdc = mf35xDiagRawAdc();
  rec.diagState = mf35xDiagStateNow();
  rec.gpio11 = schaltausgangAktiv ? 1 : 0;
  rec.state = MF35X_DIAG_PENDING;
  rec.crc32 = mf35xDiagRecordCrc(rec);

  // Kein direkter PATCH hier. Diagnose ist weniger wichtig als der
  // Basissample; die bestehende Flash-Schutzlogik entscheidet bei Platzmangel.
  mf35xDiagQueueAppend(raceId, rec);
}

void mf35xRaceQueueRpmDiagLocal(const String& raceId, uint32_t sequence) {
  if (!offlineBufferReady || !offlineRaceIdGueltig(raceId) || sequence == 0) return;

  const Mf35xRpmDiagRecord rec = mf35xRpmDiagBuild(sequence);

  // Kein direkter PATCH hier. Nur lokale Companion-Queue.
  mf35xRpmDiagQueueAppend(raceId, rec);
}

void mf35xRacePersistOne() {
  // Aufnahme-Konfiguration fuer den Capture-Task synchronisieren.
  mf35xRaceTimingConfigSync();

  // Falls der Capture-Task nicht gestartet werden konnte, bleibt die alte
  // Funktion als Notfall-Fallback erhalten. Im Normalbetrieb wird dieser Pfad
  // niemals benutzt.
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

  // WICHTIG: Immer zuerst dauerhaft puffern. Dadurch macht dieser Pfad
  // keinerlei Firebase-/HTTPS-Aufruf und ist nach dem Flash-Schreiben auch
  // gegen einen anschliessenden Netzausfall abgesichert.
  if (!offlineRecordDauerhaftPuffern(raceId, rec)) {
    historyFehler++;
    mf35xRacePersistErrors++;
    mf35xRaceTimingProcessed++;
    return;
  }

  mf35xRacePersisted++;
  mf35xRaceTimingProcessed++;

  // Zusatzdiagnosen ebenfalls nur lokal erfassen. Punkt 2 wird diese Werte
  // anschliessend noch in denselben atomaren Capture-Zeitpunkt integrieren.
  mf35xRaceQueueOilDiagLocal(raceId, rec.sequence);
  mf35xRaceQueueRpmDiagLocal(raceId, rec.sequence);
}

void mf35xRaceUploadTask(void*) {
  for (;;) {
    mf35xRaceBackgroundLoops++;

    if (WiFi.status() == WL_CONNECTED) {
      // Reihenfolge absichtlich Basis -> Oeldruck -> RPM. Companion-Diagnosen
      // pruefen beim Replay, ob der Basissample bereits in Firebase existiert.
      offlineDrainBearbeiten();
      mf35xDiagDrainOne();
      mf35xRpmDiagDrainOne();

      const unsigned long now = millis();
      if (mf35xDiagRaceId.length() > 0 && mf35xDiagCount > 0 &&
          (unsigned long)(now - mf35xRaceLastStatsUploadMs) >= MF35X_RACE_STATS_UPLOAD_MS) {
        mf35xRaceLastStatsUploadMs = now;
        mf35xDiagStatsUpload();
      }
    }

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
    "RACE-UPLOAD: Firebase/HTTPS fuer Rennhistorie vom Live-Loop entkoppelt"
  );
}

void mf35xRaceNoNetworkHousekeeping() {
  // V5.9.17-Maxwert-Plausibilisierung bleibt erhalten, verursacht aber keinen
  // Netzwerkzugriff.
  mf35xMaxPlausibilisieren();

  if (!recordingConfig.enabled || recordingConfig.raceId.length() == 0) {
    mf35xDiagStatsPersist(false);
  }
}
