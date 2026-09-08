#pragma once

// ==================================================
// MF35X V5.9.22 - ZENTRALE RUNTIME-VERDRAHTUNG
// ==================================================
// Punkt 13: Der Arduino-Sketch kennt keine einzelnen historischen Patch-
// Dateien mehr. Alle aktiven Subsysteme, ihre Include-Reihenfolge sowie Setup-
// und Loop-Verdrahtung sind an genau einer Stelle gebuendelt.
//
// Die bewaehrten Implementierungen bleiben intern getrennt, damit bei der
// Strukturbereinigung keine bereits getestete Logik kopiert oder umgeschrieben
// werden muss. Neue Hardwarepfade (Hall-Sensor, Kombisensor, 50-Hz-GPS) koennen
// spaeter hier sauber als Subsysteme angeschlossen werden.
// ==================================================

#include "rpm_stable_override.hpp"

void jsonLongFeld(String& json, bool& erstesFeld, const char* key, long wert) {
  jsonRaw(json, erstesFeld, key, String(wert));
}

// Interne Kompatibilitaets-/Diagnosemodule. Sie werden nur noch hier verdrahtet.
#include "v5917_patch.hpp"
#include "v5918_rpm_diagnostics.hpp"
#include "connectivity_diagnostics.hpp"
#include "race_timing_fix.hpp"
#include "fast_track_logger.hpp"
#include "race_network_isolation.hpp"

void mf35xRuntimeSetup() {
  mf35xCoreSetup();

  // Bestehende, getestete Subsysteme.
  mf35xV5917PatchSetup();
  mf35xV5918RpmDiagSetup();

  // Hintergrundaufgaben des naechsten USB-Stands.
  mf35xConnectivityDiagnosticsSetup();
  mf35xRaceTimingSetup();
  mf35xFastTrackSetup();
  mf35xRaceNetworkIsolationSetup();
}

void mf35xRuntimeLoop() {
  otaFirmwareValidierenWennBereit();
  gpsEinlesen();

  if (controlTaskHandle == nullptr) {
    drehzahlAktualisieren();
    schaltausgangAktualisieren();
  }

  // Voll-Rennsample nur lokal persistieren. Renn-HTTPS bleibt im eigenen
  // Background-Task und kann den Live-Betrieb nicht direkt blockieren.
  mf35xRacePersistOne();

  const unsigned long jetzt = millis();

  if (zeitFaellig(jetzt, letzterWlanCheck, WIFI_CHECK_INTERVAL_MS)) {
    letzterWlanCheck = jetzt;
    wlanPruefen();
  }

  if (WiFi.status() == WL_CONNECTED &&
      zeitFaellig(jetzt, letzterConfigCheck, FIREBASE_CONFIG_CHECK_MS)) {
    letzterConfigCheck = jetzt;
    firebaseKonfigurationLaden(false);
  }

  liveUpdatesBearbeiten();
  deviceDerivedDataBearbeiten();

  if (WiFi.status() == WL_CONNECTED &&
      zeitFaellig(jetzt, letzterDeviceStatus, DEVICE_STATUS_INTERVAL_MS)) {
    letzterDeviceStatus = jetzt;
    deviceStatusSenden();
  }

  if (zeitFaellig(jetzt, letzteStatusausgabe, STATUS_INTERVAL_MS)) {
    letzteStatusausgabe = jetzt;
    statusAusgeben();
  }

  // Ausschliesslich lokale/Plausibilitaetsarbeit; keine Renn-HTTP-Aufrufe.
  mf35xRaceNoNetworkHousekeeping();
}
