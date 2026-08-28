/*
  MF35X Livetracker V5.9.21 CONTROL-RESTORE
  - GPIO10/RPM/GPIO11 wieder auf direktem V5.9.15-Steuerpfad
  - GPIO11 bleibt im eigenen netzunabhaengigen 10-ms-Control-Task
  - aktuelle GPS/Firebase/Website/OTA-/Offline-Basisfunktionen bleiben erhalten
  - aggressive V5.9.18/V5.9.19 RPM-Renndiagnose ist bewusst nicht Teil dieses Builds
  - Oeldruck-Rohdiagnose bleibt erhalten, ist aber der Fahrfunktion nachgeordnet

  Prioritaet: relevante Fahrfunktionen vor Rennaufzeichnung/Diagnose.
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

// offline_race_buffer.hpp ruft diesen Hook auf. In diesem Control-Restore ist
// die RPM-Renndiagnose absichtlich deaktiviert; die Basis-Rennaufzeichnung
// darf deshalb weiterlaufen, ohne die Fahrfunktion zu beeinflussen.
void mf35xRpmDiagPrepare(uint32_t sequence);

#define setup mf35xCoreSetup
#define loop mf35xCoreLoop
#define attachInterrupt(pin, func, mode) \
  mf35xAttachStableRpmInterrupt((pin), (mode))
#include "MF35X_Livetracker_core.hpp"
#undef attachInterrupt
#undef loop
#undef setup

#include "rpm_stable_override.hpp"

void mf35xRpmDiagPrepare(uint32_t sequence) {
  (void)sequence;
}

// Kleine JSON-Hilfe fuer vorzeichenbehaftete ADS1115-Rohwerte.
void jsonLongFeld(String& json, bool& erstesFeld, const char* key, long wert) {
  jsonRaw(json, erstesFeld, key, String(wert));
}

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
