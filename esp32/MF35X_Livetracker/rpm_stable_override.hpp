#pragma once

// V5.9.21 CONTROL-RESTORE
// Bewusst auf den einfacheren V5.9.15-Steuerpfad zurueckgesetzt.
// Prioritaet: GPIO10/RPM/GPIO11 muss direkt, schnell und netzunabhaengig arbeiten.
// Renn-/Diagnosefunktionen duerfen diese Fahrfunktion nicht beeinflussen.

constexpr uint32_t MF35X_RPM_MIN_PULSABSTAND_US = 500UL;
constexpr uint8_t MF35X_RPM_PERIODEN_ANZAHL = 8;
constexpr unsigned long MF35X_RPM_BERECHNUNGSINTERVALL_MS = 50UL;
constexpr float MF35X_RPM_ANZEIGE_ALPHA = 0.35f;

portMUX_TYPE mf35xRpmMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t mf35xRpmLetzterImpulsUs = 0;
volatile uint32_t mf35xRpmPeriodenUs[MF35X_RPM_PERIODEN_ANZAHL] = {};
volatile uint8_t mf35xRpmPeriodenIndex = 0;
volatile uint8_t mf35xRpmPeriodenCount = 0;
volatile float mf35xRpmSchnell = 0.0f;

unsigned long mf35xRpmLetzteBerechnungMs = 0;
bool mf35xRpmAnzeigeInitialisiert = false;

void IRAM_ATTR mf35xStabileRpmISR() {
  const uint32_t jetztUs = micros();

  portENTER_CRITICAL_ISR(&mf35xRpmMux);

  const uint32_t vorherUs = mf35xRpmLetzterImpulsUs;
  const uint32_t abstandUs = jetztUs - vorherUs;

  // Offensichtliche Doppel-/Stoerflanken werden verworfen.
  if (vorherUs == 0 || abstandUs >= MF35X_RPM_MIN_PULSABSTAND_US) {
    if (vorherUs != 0) {
      mf35xRpmPeriodenUs[mf35xRpmPeriodenIndex] = abstandUs;
      mf35xRpmPeriodenIndex =
        (uint8_t)((mf35xRpmPeriodenIndex + 1U) % MF35X_RPM_PERIODEN_ANZAHL);

      if (mf35xRpmPeriodenCount < MF35X_RPM_PERIODEN_ANZAHL) {
        mf35xRpmPeriodenCount++;
      }
    }

    mf35xRpmLetzterImpulsUs = jetztUs;

    // Bestehende Core-Variablen weiter pflegen, damit der alte
    // 250-ms-Zaehler bei einem Task-Startfehler als Fallback nutzbar bleibt.
    rpmImpulse++;
    letzterRpmImpulsUs = jetztUs;
  }

  portEXIT_CRITICAL_ISR(&mf35xRpmMux);
}

float mf35xRobusteMittlerePeriodeUs(
  uint32_t* werte,
  uint8_t anzahl
) {
  if (anzahl == 0) return NAN;

  // Kleine Sortierung; maximal acht Werte.
  for (uint8_t i = 1; i < anzahl; i++) {
    const uint32_t key = werte[i];
    int j = (int)i - 1;

    while (j >= 0 && werte[j] > key) {
      werte[j + 1] = werte[j];
      j--;
    }
    werte[j + 1] = key;
  }

  uint8_t start = 0;
  uint8_t ende = anzahl;

  // Ab fuenf Perioden jeweils den kleinsten und groessten Wert verwerfen.
  if (anzahl >= 5) {
    start = 1;
    ende = (uint8_t)(anzahl - 1);
  }

  uint64_t summe = 0;
  uint8_t verwendet = 0;

  for (uint8_t i = start; i < ende; i++) {
    summe += werte[i];
    verwendet++;
  }

  if (verwendet == 0) return NAN;
  return (float)summe / (float)verwendet;
}

void mf35xStabileDrehzahlAktualisieren() {
  const unsigned long jetztMs = millis();

  if (!zeitFaellig(
        jetztMs,
        mf35xRpmLetzteBerechnungMs,
        MF35X_RPM_BERECHNUNGSINTERVALL_MS
      )) {
    return;
  }

  mf35xRpmLetzteBerechnungMs = jetztMs;

  uint32_t perioden[MF35X_RPM_PERIODEN_ANZAHL] = {};
  uint8_t anzahl = 0;
  uint32_t letzterImpulsUs = 0;

  portENTER_CRITICAL(&mf35xRpmMux);
  anzahl = mf35xRpmPeriodenCount;
  letzterImpulsUs = mf35xRpmLetzterImpulsUs;
  for (uint8_t i = 0; i < anzahl; i++) {
    perioden[i] = mf35xRpmPeriodenUs[i];
  }
  portEXIT_CRITICAL(&mf35xRpmMux);

  const uint32_t jetztUs = micros();

  rpmSignalOk =
    letzterImpulsUs != 0 &&
    anzahl >= 3 &&
    (uint32_t)(jetztUs - letzterImpulsUs) <= RPM_SIGNAL_TIMEOUT_US;

  if (!rpmSignalOk) {
    rpmRoh = 0.0f;
    mf35xRpmSchnell = 0.0f;
    rpm = 0.0f;
    mf35xRpmAnzeigeInitialisiert = false;
    return;
  }

  const float mittlerePeriodeUs =
    mf35xRobusteMittlerePeriodeUs(perioden, anzahl);

  if (!isfinite(mittlerePeriodeUs) || mittlerePeriodeUs <= 0.0f) {
    rpmSignalOk = false;
    rpmRoh = 0.0f;
    mf35xRpmSchnell = 0.0f;
    rpm = 0.0f;
    mf35xRpmAnzeigeInitialisiert = false;
    return;
  }

  const float frequenzHz = 1000000.0f / mittlerePeriodeUs;
  const float rohNeu = frequenzHz * 60.0f;
  const float schnellNeu = rohNeu * RPM_KALIBRIERFAKTOR;

  rpmRoh = rohNeu;
  mf35xRpmSchnell = schnellNeu;

  // Nur Anzeige/Logging glaetten; GPIO11 verwendet den schnellen Wert direkt.
  if (!mf35xRpmAnzeigeInitialisiert) {
    rpm = schnellNeu;
    mf35xRpmAnzeigeInitialisiert = true;
  } else {
    rpm = rpm + MF35X_RPM_ANZEIGE_ALPHA * (schnellNeu - rpm);
  }
}

void mf35xStabilenSchaltausgangAktualisieren() {
  const GpsSnapshot gpsDaten = gpsSnapshotLesen();

  OutputConfig cfg;
  portENTER_CRITICAL(&outputConfigMux);
  cfg = outputConfig;
  portEXIT_CRITICAL(&outputConfigMux);

  const bool speedFreigabe =
    gpsFixAktuell(gpsDaten) &&
    gpsDaten.speedValid &&
    gpsDaten.speedKmh >= cfg.speedEnableKmh;

  // Direkte, einfache Hysterese ohne Filter-Neuerfassungs-Sperre.
  if (!speedFreigabe || !rpmSignalOk) {
    schaltausgangAktiv = false;
  } else {
    if (!schaltausgangAktiv && mf35xRpmSchnell >= cfg.rpmOn) {
      schaltausgangAktiv = true;
    }

    if (schaltausgangAktiv && mf35xRpmSchnell < cfg.rpmOff) {
      schaltausgangAktiv = false;
    }
  }

  digitalWrite(
    SCHALTAUSGANG_PIN,
    schaltausgangAktiv ? HIGH : LOW
  );
}

void mf35xStabilerControlTask(void* parameter) {
  (void)parameter;
  TickType_t letzterStart = xTaskGetTickCount();

  for (;;) {
    mf35xStabileDrehzahlAktualisieren();
    mf35xStabilenSchaltausgangAktualisieren();
    vTaskDelayUntil(&letzterStart, CONTROL_TASK_PERIOD_TICKS);
  }
}

void mf35xAttachStableRpmInterrupt(int pin, int mode) {
  ::attachInterrupt(pin, mf35xStabileRpmISR, mode);

  BaseType_t ergebnis = xTaskCreatePinnedToCore(
    mf35xStabilerControlTask,
    "MF35X_RPM_CONTROL",
    CONTROL_TASK_STACK_SIZE,
    nullptr,
    CONTROL_TASK_PRIORITY,
    &controlTaskHandle,
    CONTROL_TASK_CORE
  );

  if (ergebnis == pdPASS) {
    Serial.println(
      "GPIO10/GPIO11 CONTROL-RESTORE: direkter V5.9.15-Steuerpfad aktiv"
    );
  } else {
    controlTaskHandle = nullptr;
    Serial.println(
      "WARNUNG: RPM/GPIO11-Control-Task konnte nicht gestartet werden - Core-Fallback aktiv."
    );
  }
}
