/*
  MF35X Livetracker V5.9.17 OTA SIGNED
  - robuste Drehzahlauswertung am W-Anschluss mit Median + Plausibilitaetsfilter
  - schnelle GPIO11-Steuerung bleibt netzunabhaengig
  - stromausfallsichere Maxwert-Generationen + Oeldruck-Renndiagnose

  Diese .ino-Datei bleibt absichtlich minimal.
  Der eigentliche Tracker-Code liegt in MF35X_Livetracker_core.hpp.
*/

// Die Core-Abhaengigkeiten vorab laden. Dadurch kann direkt vor dem Core-Include
// nur der eine setup()-Aufruf von attachInterrupt gezielt umgeleitet werden,
// ohne Deklarationen aus Arduino-/Bibliotheks-Headern zu beeinflussen.
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
#include "oil_pressure_diagnostics.hpp"

void mf35xAttachStableRpmInterrupt(int pin, int mode);

#define attachInterrupt(pin, func, mode) \
  mf35xAttachStableRpmInterrupt((pin), (mode))
#include "MF35X_Livetracker_core.hpp"
#undef attachInterrupt

#include "rpm_stable_override.hpp"
