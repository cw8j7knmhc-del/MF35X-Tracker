/*
  MF35X Livetracker - naechster USB-Arbeitsstand auf Basis V5.9.19
  - V5.9.19 bleibt der derzeitige Referenzstand am Fahrzeug
  - Punkt 1: Renn-Sampling zeitlich von HTTPS/Firebase entkoppelt
  - Punkt 1B: Renn-Upload/Diagnose komplett aus der Live-loop ausgelagert
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
#include "race_network_isolation.hpp"

void setup() {
  mf35xCoreSetup();
  mf35xV5917PatchSetup();
  mf35xV5918RpmDiagSetup();
  mf35xRaceTimingSetup();
  mf35xRaceNetworkIsolationSetup();
}

// Punkt 1 / 1B:
// Der bisherige mf35xCoreLoop() wird fuer den naechsten USB-Arbeitsstand
// bewusst nicht direkt aufgerufen. Sein Ablauf bleibt funktional gleich,
// aber alle rennbezogenen Netzwerkzugriffe sind aus der normalen loop()
// entfernt.
void mf35xNextUsbCoreLoop() {
  otaFirmwareValidierenWennBereit();
  gpsEinlesen();

  // Normalfall: RPM/GPIO11 laufen im eigenen Steuerungs-Task.
  if (controlTaskHandle == nullptr) {
    drehzahlAktualisieren();
    schaltausgangAktualisieren();
  }

  // Rennsample aus dem exakten Capture-Task nur schnell in LittleFS sichern.
  // KEIN Firebase-/HTTPS-Zugriff in diesem Pfad.
  mf35xRacePersistOne();

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

  // WICHTIG: offlineDrainBearbeiten() wird hier NICHT mehr aufgerufen.
  // Renn-Basisdaten und Companion-Diagnosen werden ausschliesslich vom
  // mf35x_race_upload Background-Task nach Firebase gesendet.

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
  mf35xNextUsbCoreLoop();

  // Nur lokale/Plausibilitaets-Hausarbeit. Die bisherigen V5.9.17/18-
  // Patch-Loops werden hier absichtlich NICHT mehr aufgerufen, weil diese
  // rennbezogene Firebase-PATCH/GET-Aufrufe enthalten. Diagnose-Capture und
  // Nachsenden laufen jetzt ueber race_network_isolation.hpp.
  mf35xRaceNoNetworkHousekeeping();
}
