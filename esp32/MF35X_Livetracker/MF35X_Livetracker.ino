/*
  MF35X Livetracker V5.9.22 - vorbereiteter naechster USB-Stand

  Aktiver Stand:
  - Punkt 1: Renn-Sampling + Renn-Netzwerk vom Live-Betrieb entkoppelt
  - Punkt 2: atomare Voll-Rennsamples
  - Punkt 5: separater 10-Hz-Fast-Track, vorbereitet fuer 50-Hz-GPS
  - Punkt 8: saubere Versionsfolge V5.9.22 / 50922
  - Punkt 9: automatisches Release-Gate + Compile-Test
  - Punkt 12: WLAN/Gateway/DNS/Firebase + optionale RUT200-LTE-Diagnose
  - Punkt 13: zentrale Runtime-Verdrahtung statt Patch-Verkettung im Sketch

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

// Der historische Core bleibt unveraendert eingebunden. Nur setup()/loop() und
// der Interrupt-Hook werden fuer die V5.9.22-Runtime sauber uebernommen.
#define setup mf35xCoreSetup
#define loop mf35xCoreLoop
#define attachInterrupt(pin, func, mode) \
  mf35xAttachStableRpmInterrupt((pin), (mode))
#include "MF35X_Livetracker_core.hpp"
#undef attachInterrupt
#undef loop
#undef setup

#include "mf35x_runtime.hpp"

void setup() {
  mf35xRuntimeSetup();
}

void loop() {
  mf35xRuntimeLoop();
}
