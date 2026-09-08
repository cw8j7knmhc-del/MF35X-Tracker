#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <string.h>

// ==================================================
// NAECHSTER USB-STAND - PUNKT 1 + PUNKT 2
// DETERMINISTISCHE + ATOMARE RENNAUFZEICHNUNG
// ==================================================
// Punkt 1:
// - eigener FreeRTOS-Task fuer den exakten Capture-Zeitpunkt
// - keine HTTP-/Flash-Zugriffe im Capture-Task
//
// Punkt 2:
// - Basis-Rennrecord, Oeldruckdiagnose und RPM/GPIO11-Diagnose werden
//   innerhalb EINES Capture-Vorgangs eingefroren
// - alle drei Datensaetze erhalten spaeter dieselbe bootId/sequence
// - GPIO11 wird genau einmal gelesen und fuer Basis + beide Diagnosen benutzt
// - Basis-Oeldruck wird aus derselben eingefrorenen AIN1-Spannung berechnet,
//   aus der auch die Oeldruckdiagnose entsteht
// - Basis-RPM wird aus demselben eingefrorenen RPM-Snapshot abgeleitet wie die
//   RPM-Diagnose
// ==================================================

constexpr uint8_t MF35X_RACE_TIMING_QUEUE_LEN = 32;
constexpr uint32_t MF35X_RACE_TIMING_TASK_STACK = 4096;
constexpr UBaseType_t MF35X_RACE_TIMING_TASK_PRIORITY = 2;
constexpr BaseType_t MF35X_RACE_TIMING_TASK_CORE = 1;
constexpr TickType_t MF35X_RACE_TIMING_POLL_TICKS = pdMS_TO_TICKS(5);
constexpr size_t MF35X_RACE_ID_BUFFER_LEN = 49;

struct Mf35xRaceTimingConfig {
  bool enabled;
  uint32_t intervalMs;
  uint32_t revision;
  char raceId[MF35X_RACE_ID_BUFFER_LEN];
};

struct Mf35xTimedRaceCapture {
  OfflineRaceRecord rec;
  Mf35xOilDiagRecord oilDiag;
  Mf35xRpmDiagRecord rpmDiag;
  float oilPressureBarForStats;
  uint32_t captureWindowUs;
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
volatile uint32_t mf35xRaceAtomicLastWindowUs = 0;
volatile uint32_t mf35xRaceAtomicMaxWindowUs = 0;

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

float mf35xRaceOilPressureFinalFromVoltage(float v) {
  if (!adsOk || !isfinite(v)) return NAN;
  if (v < 0.02f) return NAN;
  if (v > 3.10f || v >= DRUCK_VCC - 0.02f) return NAN;

  const float ohm = DRUCK_R_FIXED * v / (DRUCK_VCC - v);
  float bar = widerstandZuBar(ohm);
  if (isfinite(bar) && bar < 0.55f) bar = 0.0f;
  return bar;
}

float mf35xRaceOilTempFromVoltage(float v) {
  if (!adsOk || !isfinite(v) || v <= 0.05f || v >= 3.25f) return NAN;

  const float ohm = OEL_R_FIXED * v / (OEL_VCC - v);
  float temp =
    OEL_CAL_T1 +
    ((OEL_CAL_R1 - ohm) * (OEL_CAL_T2 - OEL_CAL_T1) /
     (OEL_CAL_R1 - OEL_CAL_R2));
  temp += OEL_TEMP_OFFSET;
  return temp;
}

float mf35xRaceBatteryFromAdcVoltage(float v) {
  if (!adsOk || !isfinite(v) || v < 0.02f || v > 3.25f) return NAN;
  return v * BAT_TEILERFAKTOR * BAT_KORREKTUR;
}

uint8_t mf35xRaceOilDiagStateFromVoltage(bool adsOkAtCapture, float v) {
  if (!adsOkAtCapture) return MF35X_OP_ADS_ERROR;
  if (!isfinite(v)) return MF35X_OP_INVALID;
  if (v < 0.02f) return MF35X_OP_SHORT;
  if (v > 2.50f) return MF35X_OP_OPEN;

  const float ohm = mf35xDiagOhmFromVoltage(v);
  if (!isfinite(ohm)) return MF35X_OP_INVALID;
  if (ohm < 5.0f || ohm > 250.0f) return MF35X_OP_OUT_OF_RANGE;
  return MF35X_OP_OK;
}

Mf35xOilDiagRecord mf35xRaceOilDiagSnapshotBauen(
  float pressureVoltage,
  bool adsOkAtCapture,
  bool switchState
) {
  Mf35xOilDiagRecord rec = {};
  rec.magic = MF35X_DIAG_MAGIC;
  rec.version = MF35X_DIAG_VERSION;
  rec.size = sizeof(rec);
  rec.bootId = offlineBootId;
  rec.sequence = 0;

  if (isfinite(pressureVoltage)) {
    long raw = lroundf(pressureVoltage / MF35X_DIAG_ADS_LSB_V);
    if (raw < 0) raw = 0;
    if (raw > INT16_MAX) raw = INT16_MAX;
    rec.rawAdc = (int16_t)raw;
  } else {
    rec.rawAdc = 0;
  }

  rec.diagState =
    mf35xRaceOilDiagStateFromVoltage(adsOkAtCapture, pressureVoltage);
  rec.gpio11 = switchState ? 1 : 0;
  rec.state = MF35X_DIAG_PENDING;
  rec.crc32 = mf35xDiagRecordCrc(rec);
  return rec;
}

Mf35xRpmDiagRecord mf35xRaceRpmDiagSnapshotBauen(
  bool& rpmValidOut,
  uint16_t& rpmValueOut,
  bool& switchStateOut
) {
  Mf35xRpmDiagRecord rec = {};
  rec.magic = MF35X_RPM_DIAG_MAGIC;
  rec.version = MF35X_RPM_DIAG_VERSION;
  rec.size = sizeof(rec);
  rec.bootId = offlineBootId;
  rec.sequence = 0;
  rec.state = MF35X_RPM_DIAG_PENDING;

  float rawRpm = 0.0f;
  float filteredRpm = 0.0f;
  float displayRpm = 0.0f;
  uint32_t rawEdges = 0;
  uint32_t acceptedEdges = 0;
  uint32_t rejectedEdges = 0;
  uint32_t doubleEdges = 0;
  uint32_t reacquires = 0;
  uint32_t referenceUs = 0;
  uint8_t periodCount = 0;

  portENTER_CRITICAL(&mf35xRpmMux);
  rawRpm = mf35xRpmRohUngefiltert;
  filteredRpm = mf35xRpmSchnell;
  displayRpm = rpm;
  rawEdges = mf35xRpmRohImpulseGesamt;
  acceptedEdges = mf35xRpmAkzeptierteImpulseGesamt;
  rejectedEdges = mf35xRpmVerworfeneImpulse;
  doubleEdges = mf35xRpmDoppelImpulse;
  reacquires = mf35xRpmNeuerfassungen;
  referenceUs = mf35xRpmReferenzPeriodeUs;
  periodCount = mf35xRpmPeriodenCount;
  portEXIT_CRITICAL(&mf35xRpmMux);

  // Diese beiden Zustandswerte werden genau einmal gelesen und danach sowohl
  // fuer Basisrecord als auch Diagnose verwendet.
  const bool signalOkAtCapture = rpmSignalOk;
  const bool switchAtCapture = schaltausgangAktiv;

  rec.rawRpmDeci = mf35xRpmDiagDeci(rawRpm);
  rec.filteredRpmDeci = mf35xRpmDiagDeci(filteredRpm);
  rec.displayRpmDeci = mf35xRpmDiagDeci(displayRpm);
  rec.rawEdgesTotal = rawEdges;
  rec.acceptedEdgesTotal = acceptedEdges;
  rec.rejectedEdgesTotal = rejectedEdges;
  rec.doubleEdgesTotal = doubleEdges;
  rec.reacquireTotal = reacquires;
  rec.referencePeriodUs = referenceUs;
  rec.rejectedSinceLastSample =
    mf35xRpmDiagDelta16(rejectedEdges, mf35xRpmDiagLastRejected);
  rec.doubleSinceLastSample =
    mf35xRpmDiagDelta16(doubleEdges, mf35xRpmDiagLastDouble);
  rec.periodCount = periodCount;

  mf35xRpmDiagLastRejected = rejectedEdges;
  mf35xRpmDiagLastDouble = doubleEdges;

  if (signalOkAtCapture) rec.flags |= MF35X_RPM_DIAG_FLAG_SIGNAL_OK;
  if (referenceUs != 0 && periodCount >= MF35X_RPM_MIN_PERIODEN) {
    rec.flags |= MF35X_RPM_DIAG_FLAG_FILTER_LOCKED;
  }
  if (switchAtCapture) rec.flags |= MF35X_RPM_DIAG_FLAG_GPIO11;
  if (isfinite(rawRpm)) rec.flags |= MF35X_RPM_DIAG_FLAG_RAW_VALID;
  if (signalOkAtCapture && isfinite(filteredRpm)) {
    rec.flags |= MF35X_RPM_DIAG_FLAG_FILTERED_VALID;
  }
  if (signalOkAtCapture && isfinite(displayRpm)) {
    rec.flags |= MF35X_RPM_DIAG_FLAG_DISPLAY_VALID;
  }

  rpmValidOut =
    (rec.flags & MF35X_RPM_DIAG_FLAG_DISPLAY_VALID) != 0;
  rpmValueOut = rpmValidOut
    ? (uint16_t)(((uint32_t)rec.displayRpmDeci + 5UL) / 10UL)
    : 0U;
  switchStateOut = switchAtCapture;

  rec.crc32 = mf35xRpmDiagRecordCrc(rec);
  return rec;
}

Mf35xTimedRaceCapture mf35xRaceAtomicCaptureBauen(const char* raceId) {
  Mf35xTimedRaceCapture item = {};
  const uint32_t captureStartUs = micros();

  OfflineRaceRecord& rec = item.rec;
  rec.magic = OFFLINE_RECORD_MAGIC;
  rec.version = OFFLINE_RECORD_VERSION;
  rec.size = sizeof(OfflineRaceRecord);
  rec.bootId = offlineBootId;
  rec.sequence = 0;
  rec.capturedMillis = millis();
  rec.state = OFFLINE_STATE_PENDING;

  bool rpmValid = false;
  uint16_t rpmValue = 0;
  bool switchState = false;
  item.rpmDiag =
    mf35xRaceRpmDiagSnapshotBauen(rpmValid, rpmValue, switchState);

  // AIN-Werte genau einmal einfrieren. Die Basiswerte werden aus denselben
  // Spannungen neu berechnet; damit koennen Basis und Diagnose nicht mehr aus
  // zwei verschiedenen Sensorzyklen stammen.
  const bool adsOkAtCapture = adsOk;
  const float pressureVoltage = oilPressureVoltage;
  const float oilTempVoltage = oilVoltage;
  const float batteryAdcAtCapture = batteryAdcVoltage;
  const double cylinderAtCapture = cylinderTemp;

  const float pressureBar =
    mf35xRaceOilPressureFinalFromVoltage(pressureVoltage);
  const float oilTempAtCapture =
    mf35xRaceOilTempFromVoltage(oilTempVoltage);
  const float batteryAtCapture =
    mf35xRaceBatteryFromAdcVoltage(batteryAdcAtCapture);

  item.oilDiag = mf35xRaceOilDiagSnapshotBauen(
    pressureVoltage,
    adsOkAtCapture,
    switchState
  );
  item.oilPressureBarForStats = pressureBar;

  const GpsSnapshot gpsDaten = gpsSnapshotLesen();
  const bool gpsGueltig = gpsFixAktuell(gpsDaten);

  if (gpsGueltig) {
    rec.flags |= OFFLINE_FLAG_GPS_VALID;
    rec.latE6 = (int32_t)llround(gpsDaten.lat * 1000000.0);
    rec.lngE6 = (int32_t)llround(gpsDaten.lng * 1000000.0);
  }

  if (gpsGueltig && gpsDaten.speedValid) {
    rec.flags |= OFFLINE_FLAG_SPEED_VALID;
    rec.speedDeci =
      offlineSkaliertSigned((float)gpsDaten.speedKmh, 10.0f);
  }

  if (gpsDaten.hdopValid) {
    rec.flags |= OFFLINE_FLAG_HDOP_VALID;
    rec.hdopCenti =
      offlineSkaliertUnsigned((float)gpsDaten.hdop, 100.0f);
  }

  if (gpsDaten.satellitesValid) {
    rec.flags |= OFFLINE_FLAG_SATELLITES_VALID;
    rec.satellites =
      (uint8_t)(gpsDaten.satellites > 255U ? 255U : gpsDaten.satellites);
  }

  if (gpsDaten.utcValid && gpsDaten.utcEpochMs > 1700000000000ULL) {
    const unsigned long delta =
      (unsigned long)(rec.capturedMillis - gpsDaten.utcUpdateMillis);
    if (delta <= 10000UL) {
      rec.capturedEpochMs = gpsDaten.utcEpochMs + (uint64_t)delta;
      rec.flags |= OFFLINE_FLAG_CAPTURE_TIME_VALID;
    }
  }

  if (rpmValid) {
    rec.flags |= OFFLINE_FLAG_RPM_VALID;
    rec.rpmValue = rpmValue;
  }

  if (isfinite(pressureBar)) {
    rec.flags |= OFFLINE_FLAG_OIL_PRESSURE_VALID;
    rec.oilPressureCenti =
      offlineSkaliertSigned(pressureBar, 100.0f);
  }

  if (isfinite(oilTempAtCapture)) {
    rec.flags |= OFFLINE_FLAG_OIL_TEMP_VALID;
    rec.oilTempDeci =
      offlineSkaliertSigned(oilTempAtCapture, 10.0f);
  }

  if (isfinite(batteryAtCapture)) {
    rec.flags |= OFFLINE_FLAG_BATTERY_VALID;
    rec.batteryCenti =
      offlineSkaliertUnsigned(batteryAtCapture, 100.0f);
  }

  if (isfinite(cylinderAtCapture)) {
    rec.flags |= OFFLINE_FLAG_CYLINDER_VALID;
    rec.cylinderTempDeci =
      offlineSkaliertSigned((float)cylinderAtCapture, 10.0f);
  }

  if (switchState) {
    rec.flags |= OFFLINE_FLAG_SWITCH_OUTPUT;
  }

  rec.wifiRssi =
    WiFi.status() == WL_CONNECTED ? (int16_t)WiFi.RSSI() : (int16_t)-127;

  strncpy(item.raceId, raceId ? raceId : "", sizeof(item.raceId) - 1);

  // CRCs werden nach Vergabe der gemeinsamen finalen Sequenz neu berechnet.
  rec.crc32 = 0;
  item.oilDiag.crc32 = 0;
  item.rpmDiag.crc32 = 0;

  item.captureWindowUs = (uint32_t)(micros() - captureStartUs);
  mf35xRaceAtomicLastWindowUs = item.captureWindowUs;
  if (item.captureWindowUs > mf35xRaceAtomicMaxWindowUs) {
    mf35xRaceAtomicMaxWindowUs = item.captureWindowUs;
  }

  return item;
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
      Mf35xTimedRaceCapture item =
        mf35xRaceAtomicCaptureBauen(cfg.raceId);

      if (lastCaptureMs != 0) {
        const uint32_t delta =
          (uint32_t)(item.rec.capturedMillis - lastCaptureMs);
        mf35xRaceTimingLastDeltaMs = delta;
        const uint32_t jitter =
          delta > cfg.intervalMs
            ? delta - cfg.intervalMs
            : cfg.intervalMs - delta;
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
    "RACE-TIMING: exakter + atomarer Capture-Task aktiv"
  );
}

// Kompatibilitaetsfunktion. Der normale next-usb-Pfad verwendet
// mf35xRacePersistOne() aus race_network_isolation.hpp.
void mf35xRaceTimingBearbeiten() {
  mf35xRaceTimingConfigSync();

  if (!mf35xRaceTimingQueue || !mf35xRaceTimingTaskHandle) {
    rennhistorieBearbeiten();
    return;
  }

  Mf35xTimedRaceCapture item = {};
  if (xQueueReceive(mf35xRaceTimingQueue, &item, 0) != pdTRUE) return;

  const uint32_t sequence = ++offlineSampleSequence;
  item.rec.sequence = sequence;
  item.oilDiag.sequence = sequence;
  item.rpmDiag.sequence = sequence;

  item.rec.crc32 = offlineRecordCrc(item.rec);
  item.oilDiag.crc32 = mf35xDiagRecordCrc(item.oilDiag);
  item.rpmDiag.crc32 = mf35xRpmDiagRecordCrc(item.rpmDiag);

  const String raceId(item.raceId);
  if (offlineRecordDauerhaftPuffern(raceId, item.rec)) {
    if (isfinite(item.oilPressureBarForStats)) {
      if (mf35xDiagRaceId != raceId) mf35xDiagStatsReset(raceId);
      mf35xDiagStatsAdd(item.oilPressureBarForStats);
    }
    mf35xDiagQueueAppend(raceId, item.oilDiag);
    mf35xRpmDiagQueueAppend(raceId, item.rpmDiag);
  } else {
    historyFehler++;
  }

  mf35xRaceTimingProcessed++;
}

uint32_t mf35xRaceTimingQueuePending() {
  return mf35xRaceTimingQueue
    ? (uint32_t)uxQueueMessagesWaiting(mf35xRaceTimingQueue)
    : 0UL;
}
