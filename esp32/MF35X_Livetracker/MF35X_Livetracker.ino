/*
  MF35X Livetracker V5.9.17 OTA SIGNED
  - robuste Drehzahlauswertung am W-Anschluss mit Median + Plausibilitaetsfilter
  - schnelle GPIO11-Steuerung bleibt netzunabhaengig
  - robuster ESP32-Maximalwert/Reset-Patch
  - Oeldruck-Rohdiagnose fuer Rennaufzeichnung

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

// Core-setup()/loop() umbenennen, damit V5.9.17 die zwei vorbereiteten
// Zusatzfunktionen sauber vor/nach dem unveraenderten Kern einhaengen kann.
#define setup mf35xCoreSetup
#define loop mf35xCoreLoop
#define attachInterrupt(pin, func, mode) \
  mf35xAttachStableRpmInterrupt((pin), (mode))
#include "MF35X_Livetracker_core.hpp"
#undef attachInterrupt
#undef loop
#undef setup

#include "rpm_stable_override.hpp"
#include "v5917_patch.hpp"

void setup() {
  mf35xCoreSetup();
  mf35xV5917PatchSetup();
}

void loop() {
  const uint32_t raceSequenceBefore = offlineSampleSequence;
  mf35xCoreLoop();
  mf35xV5917PatchLoop(raceSequenceBefore);
}
