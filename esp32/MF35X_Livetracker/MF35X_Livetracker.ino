/*
  MF35X Livetracker - naechster USB-Arbeitsstand auf Basis V5.9.19
  - V5.9.19 bleibt der derzeitige Referenzstand am Fahrzeug
  - Punkt 1: Renn-Sampling zeitlich von HTTPS/Firebase entkoppelt
  - robuste Drehzahlauswertung am W-Anschluss mit Median + Plausibilitaetsfilter
  - zusaetzliche 0,5x-Doppelflankensperre gegen nahezu exakt doppelte RPM
  - schnelle GPIO11-Steuerung verwendet ausschliesslich plausibilisierte RPM
  - RPM-Roh-/Filter-/Verwerfungsdiagnose in der Rennaufzeichnung
  - robuster ESP32-Maximalwert/Reset-Patch
  - Oeldruck-Rohdiagnose fuer Rennaufzeichnung

  Dieser Branch ist der vorbereitete naechste USB-Stand.
  main / die laufende V5.9.19-Firmware wird dadurch nicht veraendert.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <TinyGPS++.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_MAX31855.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <limits.h>
#include "firmware_version.h"
#include "ota_public_key.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

void mf35xAttachStableRpmInterrupt(int pin, int mode);

// Core-setup()/loop() umbenennen, damit die vorbereiteten Zusatzfunktionen
// sauber vor/nach dem Kern eingehangen werden koennen.
#define setup mf35xCoreSetup
#define loop mf35xCoreLoop
#define attachInterrupt(pin, func, mode) \
  mf35xAttachStableRpmInterrupt((pin), (mode))
#include "MF35X_Livetracker_core.hpp"
#undef attachInterrupt
#undef loop
#undef setup

#include "rpm_stable_override.hpp"

// Kleine JSON-Hilfe fuer vorzeichenbehaftete ADS1115-Rohwerte.
void jsonLongFeld(String& json, bool& erstesFeld, const char* key, long wert) {
  jsonRaw(json, erstesFeld, key, String(wert));
}

#include "v5917_patch.hpp"
#include "v5918_rpm_diagnostics.hpp"
#include "race_timing_fix.hpp"

void setup() {
  mf35xCoreSetup();
  mf35xV5917PatchSetup();
  mf35xV5918RpmDiagSetup();
  mf35xRaceTimingSetup();
}

// Punkt 1:
// Der bisherige mf35xCoreLoop() wird fuer den naechsten USB-Arbeitsstand
// bewusst nicht direkt aufgerufen. Sein Ablauf bleibt hier gleich, nur die
// Rennaufzeichnung wird GANZ VOR die blockierenden Firebase/HTTPS-Arbeiten
// gezogen und durch den eigenen Race-Timing-Task bedient.
void mf35xNextUsbCoreLoop() {
  otaFirmwareValidierenWennBereit();
  gpsEinlesen();

  // Normalfall: RPM/GPIO11 laufen im eigenen Steuerungs-Task.
  if (controlTaskHandle == nullptr) {
    drehzahlAktualisieren();
    schaltausgangAktualisieren();
  }

  // Zuerst einen eventuell exakt getakteten Renn-Snapshot verarbeiten.
  // Ein HTTPS-Timeout weiter unten kann dadurch keinen Capture-Zeitpunkt mehr
  // verschieben; waehrend einer Blockade sammelt der Capture-Task weiter.
  mf35xRaceTimingBearbeiten();

  unsigned long jetzt = millis();

  if (zeitFaellig(jetzt, letzterWlanCheck, WIFI_CHECK_INTERVAL_MS)) {
    letzterWlanCheck = jetzt;
    wlanPruefen();
  }

  if (WiFi.status() == WL_CONNECTED &&
      zeitFaellig(jetzt, letzterConfigCheck, FIREBASE_CONFIG_CHECK_MS)) {
    letzterConfigCheck = jetzt;
    firebaseKonfigurationLaden(false);
  }

  // Alle Website-Intervalle werden hier wirksam.
  liveUpdatesBearbeiten();

  // NVS-Sicherung, Maxwert-Sync und Alarm-Nachsenden.
  deviceDerivedDataBearbeiten();

  // Maximal einen dauerhaft gepufferten Datensatz pro Drain-Zyklus nachsenden.
  offlineDrainBearbeiten();

  if (WiFi.status() == WL_CONNECTED &&
      zeitFaellig(jetzt, letzterDeviceStatus, DEVICE_STATUS_INTERVAL_MS)) {
    letzterDeviceStatus = jetzt;
    deviceStatusSenden();
  }

  if (zeitFaellig(jetzt, letzteStatusausgabe, STATUS_INTERVAL_MS)) {
    letzteStatusausgabe = jetzt;
    statusAusgeben();
  }
}

void loop() {
  // offlineSampleSequence wird beim neuen Timing erst dann erhoeht, wenn der
  // Basissample in der normalen loop() tatsaechlich verarbeitet wird. Dadurch
  // bleiben die V5.9.17/V5.9.18-Diagnosebegleiter 1:1 zum Basissample erhalten.
  const uint32_t raceSequenceBefore = offlineSampleSequence;

  mf35xNextUsbCoreLoop();

  mf35xV5917PatchLoop(raceSequenceBefore);
  mf35xV5918RpmDiagLoop(raceSequenceBefore);
}
