#pragma once

// Robuste W-Signal-Auswertung fuer V5.9.19.
// Ziel:
// - kurze Stoerflanken und echte Doppel-Flanken nicht als Motordrehzahl werten
// - bei fehlenden Pulsen eine 2x/3x/4x-Luecke korrekt normalisieren
// - GPIO11 weiterhin im schnellen, netzunabhaengigen Control-Task steuern
// - Roh-/Filterzaehler fuer die Rennanalyse bereitstellen
// - bei Neuerfassung niemals direkt auf eine 0,5x-Doppelflanke einlernen

constexpr uint32_t MF35X_RPM_MIN_PULSABSTAND_US = 500UL;
constexpr uint8_t MF35X_RPM_PERIODEN_ANZAHL = 21;
constexpr uint8_t MF35X_RPM_MIN_PERIODEN = 7;
constexpr unsigned long MF35X_RPM_BERECHNUNGSINTERVALL_MS = 50UL;
constexpr unsigned long MF35X_RPM_RAW_DIAG_INTERVALL_MS = 250UL;
constexpr float MF35X_RPM_ANZEIGE_ALPHA = 0.20f;

// Nach dem Einlernen muss ein einzelner Pulsabstand innerhalb +/- 8 % der
// laufend nachgefuehrten Referenz liegen. Reale Motordrehzahl kann sich von
// einem Lichtmaschinenimpuls zum naechsten nicht sprunghaft um diesen Betrag
// aendern; Stoerflanken koennen das sehr wohl.
constexpr uint32_t MF35X_RPM_PLAUS_MIN_PROZENT = 92UL;
constexpr uint32_t MF35X_RPM_PLAUS_MAX_PROZENT = 108UL;
constexpr uint8_t MF35X_RPM_MAX_LUECKENFAKTOR = 4;
constexpr uint8_t MF35X_RPM_NEUERFASSUNG_NACH_FEHLERN = 12;

// Neuerfassung V5.9.19:
// Eine neue Referenz wird erst nach mehreren zueinander passenden Rohperioden
// uebernommen. Damit kann eine einzelne Stoerflanke nicht mehr sofort die
// Referenz halbieren und dadurch eine nahezu doppelte Drehzahl erzeugen.
constexpr uint8_t MF35X_RPM_NEUERFASSUNG_BESTAETIGUNGEN = 7;
constexpr uint32_t MF35X_RPM_NEUERFASSUNG_MIN_PROZENT = 88UL;
constexpr uint32_t MF35X_RPM_NEUERFASSUNG_MAX_PROZENT = 112UL;

// Typischer Fehler aus der Rennaufzeichnung: ein zusaetzlicher Puls liegt etwa
// in der Mitte zwischen zwei echten W-Pulsen. Er wuerde die Drehzahl nahezu
// exakt verdoppeln. Solche ~0,5x-Perioden werden separat erkannt und duerfen
// die Neuerfassung NICHT auf eine falsche 2x-Frequenz umschalten.
constexpr uint32_t MF35X_RPM_DOPPEL_MIN_PROZENT = 42UL;
constexpr uint32_t MF35X_RPM_DOPPEL_MAX_PROZENT = 58UL;

portMUX_TYPE mf35xRpmMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t mf35xRpmLetzterImpulsUs = 0;
volatile uint32_t mf35xRpmPeriodenUs[MF35X_RPM_PERIODEN_ANZAHL] = {};
volatile uint8_t mf35xRpmPeriodenIndex = 0;
volatile uint8_t mf35xRpmPeriodenCount = 0;
volatile uint32_t mf35xRpmReferenzPeriodeUs = 0;
volatile uint8_t mf35xRpmFehlerInFolge = 0;

// V5.9.19: geschuetzte Neuerfassung.
// Die alte Referenz bleibt als Doppelflanken-Schutz erhalten, bis eine neue
// Periode durch mehrere passende Rohflanken bestaetigt wurde. Falls das Signal
// zwischendurch wirklich laenger als das RPM-Timeout aussetzt, wird die alte
// Referenz verworfen und beim Wiederanlauf normal neu gelernt.
volatile bool mf35xRpmNeuerfassungAktiv = false;
volatile uint32_t mf35xRpmNeuerfassungAlteReferenzUs = 0;
volatile uint32_t mf35xRpmNeuerfassungLetzteRohflankeUs = 0;
volatile uint32_t mf35xRpmNeuerfassungKandidatUs = 0;
volatile uint8_t mf35xRpmNeuerfassungTreffer = 0;

// Diagnosezaehler. raw = jede FALLING-Flanke am GPIO10,
// accepted = fuer die Drehzahl angenommene Flanke,
// rejected = verworfene Flanke, double = davon als ~0,5x erkannt.
volatile uint32_t mf35xRpmRohImpulseGesamt = 0;
volatile uint32_t mf35xRpmAkzeptierteImpulseGesamt = 0;
volatile uint32_t mf35xRpmVerworfeneImpulse = 0;
volatile uint32_t mf35xRpmDoppelImpulse = 0;
volatile uint32_t mf35xRpmNeuerfassungen = 0;

volatile float mf35xRpmSchnell = 0.0f;
volatile float mf35xRpmRohUngefiltert = 0.0f;

unsigned long mf35xRpmLetzteBerechnungMs = 0;
unsigned long mf35xRpmRohDiagLetzteMs = 0;
uint32_t mf35xRpmRohDiagLetzterZaehler = 0;
bool mf35xRpmAnzeigeInitialisiert = false;

void IRAM_ATTR mf35xStabileRpmISR() {
  const uint32_t jetztUs = micros();

  portENTER_CRITICAL_ISR(&mf35xRpmMux);
  mf35xRpmRohImpulseGesamt++;

  // --------------------------------------------------
  // V5.9.19 - geschuetzte Neuerfassung
  // --------------------------------------------------
  if (mf35xRpmNeuerfassungAktiv) {
    const uint32_t vorherRohUs = mf35xRpmNeuerfassungLetzteRohflankeUs;
    mf35xRpmNeuerfassungLetzteRohflankeUs = jetztUs;

    if (vorherRohUs == 0) {
      portEXIT_CRITICAL_ISR(&mf35xRpmMux);
      return;
    }

    const uint32_t rohAbstandUs = jetztUs - vorherRohUs;

    // Nach einem echten Signal-/Motorstillstand ist die alte Referenz nicht
    // mehr verbindlich. Der erste Puls dient dann nur wieder als Zeitanker.
    if (rohAbstandUs > RPM_SIGNAL_TIMEOUT_US) {
      mf35xRpmNeuerfassungAlteReferenzUs = 0;
      mf35xRpmReferenzPeriodeUs = 0;
      mf35xRpmNeuerfassungKandidatUs = 0;
      mf35xRpmNeuerfassungTreffer = 0;
      mf35xRpmPeriodenIndex = 0;
      mf35xRpmPeriodenCount = 0;
      mf35xRpmLetzterImpulsUs = jetztUs;
      letzterRpmImpulsUs = jetztUs;
      portEXIT_CRITICAL_ISR(&mf35xRpmMux);
      return;
    }

    if (rohAbstandUs < MF35X_RPM_MIN_PULSABSTAND_US) {
      mf35xRpmVerworfeneImpulse++;
      portEXIT_CRITICAL_ISR(&mf35xRpmMux);
      return;
    }

    const uint32_t alteReferenzUs = mf35xRpmNeuerfassungAlteReferenzUs;
    bool halbeAltePeriode = false;

    if (alteReferenzUs != 0) {
      const uint32_t doppelMinUs =
        (alteReferenzUs * MF35X_RPM_DOPPEL_MIN_PROZENT) / 100UL;
      const uint32_t doppelMaxUs =
        (alteReferenzUs * MF35X_RPM_DOPPEL_MAX_PROZENT) / 100UL;
      halbeAltePeriode =
        rohAbstandUs >= doppelMinUs && rohAbstandUs <= doppelMaxUs;
    }

    // Genau der im Training gefundene Fehler: ~0,5x der alten Referenz darf
    // auch waehrend der Neuerfassung niemals zum neuen Zeitmass werden.
    if (halbeAltePeriode) {
      mf35xRpmVerworfeneImpulse++;
      mf35xRpmDoppelImpulse++;
      portEXIT_CRITICAL_ISR(&mf35xRpmMux);
      return;
    }

    if (mf35xRpmNeuerfassungKandidatUs == 0) {
      mf35xRpmNeuerfassungKandidatUs = rohAbstandUs;
      mf35xRpmNeuerfassungTreffer = 1;
      mf35xRpmVerworfeneImpulse++;
      portEXIT_CRITICAL_ISR(&mf35xRpmMux);
      return;
    }

    const uint32_t kandidatUs = mf35xRpmNeuerfassungKandidatUs;
    const uint32_t kandidatMinUs =
      (kandidatUs * MF35X_RPM_NEUERFASSUNG_MIN_PROZENT) / 100UL;
    const uint32_t kandidatMaxUs =
      (kandidatUs * MF35X_RPM_NEUERFASSUNG_MAX_PROZENT) / 100UL;

    if (rohAbstandUs >= kandidatMinUs && rohAbstandUs <= kandidatMaxUs) {
      const int32_t differenz =
        (int32_t)rohAbstandUs - (int32_t)mf35xRpmNeuerfassungKandidatUs;
      mf35xRpmNeuerfassungKandidatUs =
        (uint32_t)((int32_t)mf35xRpmNeuerfassungKandidatUs + differenz / 4);

      if (mf35xRpmNeuerfassungTreffer < 255U) {
        mf35xRpmNeuerfassungTreffer++;
      }
    } else {
      // Ein einzelner abweichender Rohabstand darf die Neuerfassung nicht
      // abschliessen. Er startet lediglich einen neuen Kandidaten.
      mf35xRpmNeuerfassungKandidatUs = rohAbstandUs;
      mf35xRpmNeuerfassungTreffer = 1;
    }

    if (mf35xRpmNeuerfassungTreffer >= MF35X_RPM_NEUERFASSUNG_BESTAETIGUNGEN) {
      // Erst jetzt ist die neue Referenz verbindlich. Der aktuelle Puls wird
      // wieder als Zeitanker uebernommen; die normalen Medianperioden muessen
      // danach wie beim Start mindestens MF35X_RPM_MIN_PERIODEN aufbauen.
      mf35xRpmReferenzPeriodeUs = mf35xRpmNeuerfassungKandidatUs;
      mf35xRpmPeriodenIndex = 0;
      mf35xRpmPeriodenCount = 0;
      mf35xRpmFehlerInFolge = 0;
      mf35xRpmLetzterImpulsUs = jetztUs;
      letzterRpmImpulsUs = jetztUs;
      rpmImpulse++;
      mf35xRpmAkzeptierteImpulseGesamt++;
      mf35xRpmNeuerfassungen++;

      mf35xRpmNeuerfassungAktiv = false;
      mf35xRpmNeuerfassungAlteReferenzUs = 0;
      mf35xRpmNeuerfassungLetzteRohflankeUs = 0;
      mf35xRpmNeuerfassungKandidatUs = 0;
      mf35xRpmNeuerfassungTreffer = 0;
    } else {
      mf35xRpmVerworfeneImpulse++;
    }

    portEXIT_CRITICAL_ISR(&mf35xRpmMux);
    return;
  }

  const uint32_t vorherUs = mf35xRpmLetzterImpulsUs;

  // Erster Puls dient nur als Zeitanker.
  if (vorherUs == 0) {
    mf35xRpmLetzterImpulsUs = jetztUs;
    letzterRpmImpulsUs = jetztUs;
    rpmImpulse++;
    mf35xRpmAkzeptierteImpulseGesamt++;
    portEXIT_CRITICAL_ISR(&mf35xRpmMux);
    return;
  }

  const uint32_t abstandUs = jetztUs - vorherUs;
  uint32_t periodeUs = abstandUs;
  bool gueltig = false;
  bool wahrscheinlicheDoppelflanke = false;

  if (abstandUs >= MF35X_RPM_MIN_PULSABSTAND_US) {
    const uint32_t referenzUs = mf35xRpmReferenzPeriodeUs;

    if (referenzUs == 0) {
      // Kurze Einlernphase nach Start / echtem Signalstillstand.
      gueltig = true;
    } else {
      const uint32_t minUs =
        (referenzUs * MF35X_RPM_PLAUS_MIN_PROZENT) / 100UL;
      const uint32_t maxUs =
        (referenzUs * MF35X_RPM_PLAUS_MAX_PROZENT) / 100UL;
      const uint32_t doppelMinUs =
        (referenzUs * MF35X_RPM_DOPPEL_MIN_PROZENT) / 100UL;
      const uint32_t doppelMaxUs =
        (referenzUs * MF35X_RPM_DOPPEL_MAX_PROZENT) / 100UL;

      wahrscheinlicheDoppelflanke =
        periodeUs >= doppelMinUs && periodeUs <= doppelMaxUs;

      if (!wahrscheinlicheDoppelflanke &&
          periodeUs >= minUs && periodeUs <= maxUs) {
        gueltig = true;
      } else if (!wahrscheinlicheDoppelflanke && periodeUs > maxUs) {
        // Falls ein echter W-Puls einmal fehlt, ist der naechste Abstand etwa
        // 2x, 3x ... so gross. Dann wird die Luecke auf eine Einzelperiode
        // normiert statt faelschlich als Drehzahleinbruch gewertet.
        for (uint8_t faktor = 2; faktor <= MF35X_RPM_MAX_LUECKENFAKTOR; faktor++) {
          const uint32_t normiertUs = periodeUs / faktor;
          if (normiertUs >= minUs && normiertUs <= maxUs) {
            periodeUs = normiertUs;
            gueltig = true;
            break;
          }
        }
      }
    }
  }

  if (gueltig) {
    mf35xRpmPeriodenUs[mf35xRpmPeriodenIndex] = periodeUs;
    mf35xRpmPeriodenIndex =
      (uint8_t)((mf35xRpmPeriodenIndex + 1U) % MF35X_RPM_PERIODEN_ANZAHL);

    if (mf35xRpmPeriodenCount < MF35X_RPM_PERIODEN_ANZAHL) {
      mf35xRpmPeriodenCount++;
    }

    mf35xRpmLetzterImpulsUs = jetztUs;
    mf35xRpmFehlerInFolge = 0;
    mf35xRpmAkzeptierteImpulseGesamt++;

    // Die Referenz wird bei jedem echten Puls nur sehr langsam nachgefuehrt.
    // Dadurch folgt sie realer Beschleunigung, reagiert aber kaum auf einen
    // einzelnen Randwert innerhalb des Plausibilitaetsfensters.
    if (mf35xRpmReferenzPeriodeUs != 0) {
      const int32_t differenz =
        (int32_t)periodeUs - (int32_t)mf35xRpmReferenzPeriodeUs;
      mf35xRpmReferenzPeriodeUs =
        (uint32_t)((int32_t)mf35xRpmReferenzPeriodeUs + differenz / 16);
    }

    // Bestehende Core-Variablen fuer Status/Fallback weiter pflegen.
    rpmImpulse++;
    letzterRpmImpulsUs = jetztUs;
  } else {
    mf35xRpmVerworfeneImpulse++;

    if (wahrscheinlicheDoppelflanke) {
      mf35xRpmDoppelImpulse++;
      // Wichtig: eine erkannte 0,5x-Flanke darf NICHT die Fehlerkette fuer
      // eine Neuerfassung erhoehen. Sonst koennte dauerhafte Doppelflanken-
      // Stoerung nach einigen Pulsen selbst zur falschen 2x-Referenz werden.
      mf35xRpmFehlerInFolge = 0;
    } else {
      if (mf35xRpmFehlerInFolge < 255U) {
        mf35xRpmFehlerInFolge++;
      }

      // V5.9.19: Die alte Referenz wird NICHT mehr sofort auf 0 gesetzt.
      // Stattdessen startet eine bestaetigte Neuerfassung. Dadurch kann die
      // naechste einzelne Stoerflanke nicht mehr zur falschen Referenz werden.
      if (mf35xRpmFehlerInFolge >= MF35X_RPM_NEUERFASSUNG_NACH_FEHLERN) {
        mf35xRpmNeuerfassungAktiv = true;
        mf35xRpmNeuerfassungAlteReferenzUs = mf35xRpmReferenzPeriodeUs;
        mf35xRpmNeuerfassungLetzteRohflankeUs = jetztUs;
        mf35xRpmNeuerfassungKandidatUs = 0;
        mf35xRpmNeuerfassungTreffer = 0;

        mf35xRpmPeriodenIndex = 0;
        mf35xRpmPeriodenCount = 0;
        mf35xRpmFehlerInFolge = 0;
        mf35xRpmLetzterImpulsUs = jetztUs;
        letzterRpmImpulsUs = jetztUs;
      }
    }
  }

  portEXIT_CRITICAL_ISR(&mf35xRpmMux);
}

float mf35xMedianPeriodeUs(uint32_t* werte, uint8_t anzahl) {
  if (anzahl == 0) return NAN;

  // Insertion-Sort ist fuer maximal 21 Werte klein und deterministisch.
  for (uint8_t i = 1; i < anzahl; i++) {
    const uint32_t key = werte[i];
    int j = (int)i - 1;

    while (j >= 0 && werte[j] > key) {
      werte[j + 1] = werte[j];
      j--;
    }
    werte[j + 1] = key;
  }

  if ((anzahl & 1U) != 0U) {
    return (float)werte[anzahl / 2U];
  }

  const uint32_t a = werte[(anzahl / 2U) - 1U];
  const uint32_t b = werte[anzahl / 2U];
  return ((float)a + (float)b) * 0.5f;
}

void mf35xRpmRohDiagnoseAktualisieren(unsigned long jetztMs) {
  if (mf35xRpmRohDiagLetzteMs == 0) {
    portENTER_CRITICAL(&mf35xRpmMux);
    mf35xRpmRohDiagLetzterZaehler = mf35xRpmRohImpulseGesamt;
    portEXIT_CRITICAL(&mf35xRpmMux);
    mf35xRpmRohDiagLetzteMs = jetztMs;
    return;
  }

  const unsigned long deltaMs =
    (unsigned long)(jetztMs - mf35xRpmRohDiagLetzteMs);
  if (deltaMs < MF35X_RPM_RAW_DIAG_INTERVALL_MS) return;

  uint32_t rawTotal = 0;
  portENTER_CRITICAL(&mf35xRpmMux);
  rawTotal = mf35xRpmRohImpulseGesamt;
  portEXIT_CRITICAL(&mf35xRpmMux);

  const uint32_t deltaPulse = rawTotal - mf35xRpmRohDiagLetzterZaehler;
  mf35xRpmRohDiagLetzterZaehler = rawTotal;
  mf35xRpmRohDiagLetzteMs = jetztMs;

  // Dieselbe Kalibrierung wie fuer die akzeptierte W-Frequenz, aber bewusst
  // auf ALLE GPIO10-Flanken. Bei Doppelflanken liegt dieser Wert etwa bei 2x.
  const float pulseProSekunde =
    deltaMs > 0 ? ((float)deltaPulse * 1000.0f / (float)deltaMs) : 0.0f;
  mf35xRpmRohUngefiltert = pulseProSekunde * 60.0f * RPM_KALIBRIERFAKTOR;
}

void mf35xStabileDrehzahlAktualisieren() {
  const unsigned long jetztMs = millis();
  mf35xRpmRohDiagnoseAktualisieren(jetztMs);

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
  bool neuerfassungAktiv = false;

  portENTER_CRITICAL(&mf35xRpmMux);
  anzahl = mf35xRpmPeriodenCount;
  letzterImpulsUs = mf35xRpmLetzterImpulsUs;
  neuerfassungAktiv = mf35xRpmNeuerfassungAktiv;

  for (uint8_t i = 0; i < anzahl; i++) {
    perioden[i] = mf35xRpmPeriodenUs[i];
  }
  portEXIT_CRITICAL(&mf35xRpmMux);

  const uint32_t jetztUs = micros();

  rpmSignalOk =
    !neuerfassungAktiv &&
    letzterImpulsUs != 0 &&
    anzahl >= MF35X_RPM_MIN_PERIODEN &&
    (uint32_t)(jetztUs - letzterImpulsUs) <= RPM_SIGNAL_TIMEOUT_US;

  if (!rpmSignalOk) {
    rpmRoh = 0.0f;
    mf35xRpmSchnell = 0.0f;
    rpm = 0.0f;
    mf35xRpmAnzeigeInitialisiert = false;
    return;
  }

  const float medianPeriodeUs = mf35xMedianPeriodeUs(perioden, anzahl);

  if (!isfinite(medianPeriodeUs) || medianPeriodeUs <= 0.0f) {
    rpmSignalOk = false;
    rpmRoh = 0.0f;
    mf35xRpmSchnell = 0.0f;
    rpm = 0.0f;
    mf35xRpmAnzeigeInitialisiert = false;
    return;
  }

  // Nach der kurzen Startphase liefert der Median die erste robuste Referenz.
  // Danach korrigiert er die ISR-Referenz langsam gegen langfristiges Driften.
  const uint32_t medianUs = (uint32_t)(medianPeriodeUs + 0.5f);
  portENTER_CRITICAL(&mf35xRpmMux);
  if (mf35xRpmReferenzPeriodeUs == 0) {
    mf35xRpmReferenzPeriodeUs = medianUs;
  } else {
    const int32_t differenz =
      (int32_t)medianUs - (int32_t)mf35xRpmReferenzPeriodeUs;
    mf35xRpmReferenzPeriodeUs =
      (uint32_t)((int32_t)mf35xRpmReferenzPeriodeUs + differenz / 4);
  }
  portEXIT_CRITICAL(&mf35xRpmMux);

  // Die schnelle Drehzahl basiert auf dem Median von bis zu 21 echten
  // Pulsperioden. Bei ~3200 rpm umfasst das nur wenige 10 ms und bleibt daher
  // schnell genug fuer GPIO11, ist aber gegen einzelne Ausreisser sehr robust.
  const float frequenzHz = 1000000.0f / medianPeriodeUs;
  const float rohNeu = frequenzHz * 60.0f;
  const float schnellNeu = rohNeu * RPM_KALIBRIERFAKTOR;

  rpmRoh = rohNeu;
  mf35xRpmSchnell = schnellNeu;

  // Website/Logging zusaetzlich sanft glaetten. Der Schaltausgang verwendet
  // bewusst mf35xRpmSchnell und damit NICHT diesen langsameren Anzeigewert.
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

  bool neuerfassungAktiv = false;
  portENTER_CRITICAL(&mf35xRpmMux);
  neuerfassungAktiv = mf35xRpmNeuerfassungAktiv;
  portEXIT_CRITICAL(&mf35xRpmMux);

  // GPIO11 darf ausschliesslich eine voll bestaetigte, plausibilisierte
  // Drehzahl sehen. Waehrend einer Neuerfassung bleibt der Ausgang sicher LOW.
  if (!speedFreigabe || neuerfassungAktiv || !rpmSignalOk || !isfinite(mf35xRpmSchnell)) {
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
  // Zu diesem Zeitpunkt hat setup() GPIO10 bereits als INPUT_PULLUP gesetzt.
  ::attachInterrupt(pin, mf35xStabileRpmISR, mode);

  // Den bestehenden controlTaskHandle absichtlich fuer den neuen Task nutzen.
  // Dadurch startet der alte Core-Task spaeter nicht parallel und loop() bleibt
  // ebenfalls im vorgesehenen netzunabhaengigen Task-Modus.
  BaseType_t ergebnis = xTaskCreatePinnedToCore(
    mf35xStabilerControlTask,
    "MF35X_RPM_ROBUST",
    CONTROL_TASK_STACK_SIZE,
    nullptr,
    CONTROL_TASK_PRIORITY,
    &controlTaskHandle,
    CONTROL_TASK_CORE
  );

  if (ergebnis == pdPASS) {
    Serial.println(
      "Drehzahlfilter V5.9.19: Median-21 + +/-8% + geschuetzte Neuerfassung"
    );
  } else {
    controlTaskHandle = nullptr;
    Serial.println(
      "WARNUNG: Robuster RPM-Task konnte nicht gestartet werden - Core-Fallback wird verwendet."
    );
  }
}
