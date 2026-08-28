/*
  MF35X Livetracker V5.9.21 OTA SIGNED - PREPARED
  - V5.9.20 sichere, synchronisierte Renn-/RPM-Diagnose als Basis
  - Rennsample-Erzeugung in eigenem FreeRTOS-Task, unabhaengig von Firebase/HTTP/LTE
  - Basissample + Oeldruck- + RPM/GPIO11-Diagnose gemeinsam im PSRAM-Ring
  - LittleFS bleibt die dauerhafte Queue; Netzwerk ist nur noch nachgelagerter Verbraucher
  - bestehende RPM-/GPIO11-/GPS-/Sensorlogik unveraendert

  Diese .ino-Datei bleibt absichtlich minimal.
  Der eigentliche Tracker-Code liegt in MF35X_Livetracker_core.hpp.
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

// Der alte V5.9.20-Hook bleibt fuer den sicheren Fallback vorhanden, falls der
// neue Async-Capture-Task ausnahmsweise nicht gestartet werden kann.
void mf35xRpmDiagPrepare(uint32_t sequence);

// Core-setup()/loop() umbenennen, damit die vorbereiteten Zusatzfunktionen
// sauber vor/nach dem unveraenderten Kern eingehangen werden koennen.
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
#include "v5920_safe_race_sync.hpp"
#include "v5921_async_race_logger.hpp"

void setup() {
  mf35xCoreSetup();
  mf35xV5917PatchSetup();
  mf35xV5918RpmDiagSetup();
  mf35xAsyncRaceLoggerSetup();
}

void loop() {
  // Vor jedem moeglicherweise blockierenden Netzabschnitt vorhandene Async-
  // Samples zuerst in die dauerhafte LittleFS-Queue uebernehmen.
  mf35xAsyncRaceLoggerSyncConfig();
  mf35xAsyncRaceLoggerFlush();

  const uint32_t raceSequenceBefore = offlineSampleSequence;
  mf35xCoreLoop();

  // Firebase-Konfiguration kann im Core geaendert worden sein.
  mf35xAsyncRaceLoggerSyncConfig();

  if (mf35xAsyncRaceLoggerActive()) {
    // Alles, was waehrend eines HTTP-/LTE-Blocks im separaten Capture-Task
    // entstanden ist, jetzt dauerhaft sichern. Netzwerk darf danach wieder
    // beliebig langsam sein, ohne das 1-60-s-Aufzeichnungsraster zu beeinflussen.
    mf35xAsyncRaceLoggerFlush();
    mf35xAsyncRaceLoggerMaintenance();
  } else {
    // Sicherer Fallback auf den bisherigen, bewaehrten V5.9.20-Pfad.
    mf35xV5917PatchLoop(raceSequenceBefore);
    mf35xV5920SafeRpmDiagLoop(raceSequenceBefore);
  }
}
