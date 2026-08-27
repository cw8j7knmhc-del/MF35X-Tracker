#pragma once

// ==================================================
// MF35X V5.9.17 PATCH
// 1) Maximalwerte: ESP32 bleibt alleinige Quelle, alte Firebase-Werte werden
//    nach Reset/Neustart nicht wieder eingemischt; Plausibilitaetsfilter gegen
//    Ausreisser.
// 2) Rennaufzeichnung: zusaetzliche Oeldruck-Diagnose (ADS1115-AIN1 Rohwert,
//    mV, berechneter Widerstand mit gemessenem 216-Ohm-Festwiderstand,
//    Kennlinienwert vor der 0,55-bar-Unterdrueckung, Diagnosezustand und
//    Min/Max/Mittelwert je Rennen). Online wird jeder Rennsample ergaenzt;
//    bei Netzausfall werden Diagnosepunkte lokal gepuffert, wobei die normale
//    Rennqueue immer Vorrang vor Diagnose-Daten hat.
// ==================================================

constexpr float MF35X_DIAG_PRESSURE_FIXED_OHM = 216.0f;
constexpr float MF35X_DIAG_ADS_LSB_V = 0.000125f; // ADS1115 GAIN_ONE
constexpr size_t MF35X_DIAG_FLASH_PROTECT_BYTES = 320UL * 1024UL;
constexpr unsigned long MF35X_DIAG_DRAIN_MS = 300UL;
constexpr unsigned long MF35X_DIAG_STATS_PERSIST_MS = 60000UL;

constexpr uint32_t MF35X_DIAG_MAGIC = 0x4D463137UL; // "MF17"
constexpr uint16_t MF35X_DIAG_VERSION = 1;
constexpr uint8_t MF35X_DIAG_PENDING = 0xA5;
constexpr uint8_t MF35X_DIAG_SENT = 0x5A;

enum Mf35xOilPressureDiagState : uint8_t {
  MF35X_OP_OK = 0,
  MF35X_OP_ADS_ERROR = 1,
  MF35X_OP_SHORT = 2,
  MF35X_OP_OPEN = 3,
  MF35X_OP_OUT_OF_RANGE = 4,
  MF35X_OP_INVALID = 5
};

#pragma pack(push, 1)
struct Mf35xOilDiagRecord {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t bootId;
  uint32_t sequence;
  int16_t rawAdc;
  uint8_t diagState;
  uint8_t gpio11;
  uint32_t crc32;
  uint8_t state;
  uint8_t padding[3];
};
#pragma pack(pop)

static_assert(sizeof(Mf35xOilDiagRecord) == 28, "Mf35xOilDiagRecord muss 28 Byte gross sein");

String mf35xDiagRaceId = "";
float mf35xDiagMinBar = NAN;
float mf35xDiagMaxBar = NAN;
double mf35xDiagSumBar = 0.0;
uint32_t mf35xDiagCount = 0;
unsigned long mf35xDiagLastPersistMs = 0;
unsigned long mf35xDiagLastDrainMs = 0;
uint32_t mf35xDiagQueued = 0;
uint32_t mf35xDiagReplayed = 0;
uint32_t mf35xDiagDropped = 0;

uint32_t mf35xDiagCrc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
    }
  }
  return ~crc;
}

uint32_t mf35xDiagRecordCrc(const Mf35xOilDiagRecord& rec) {
  return mf35xDiagCrc32(
    reinterpret_cast<const uint8_t*>(&rec),
    offsetof(Mf35xOilDiagRecord, crc32)
  );
}

bool mf35xDiagRecordValid(const Mf35xOilDiagRecord& rec) {
  return rec.magic == MF35X_DIAG_MAGIC &&
         rec.version == MF35X_DIAG_VERSION &&
         rec.size == sizeof(Mf35xOilDiagRecord) &&
         rec.diagState <= MF35X_OP_INVALID &&
         rec.crc32 == mf35xDiagRecordCrc(rec);
}

const char* mf35xDiagStateText(uint8_t s) {
  switch (s) {
    case MF35X_OP_OK: return "OK";
    case MF35X_OP_ADS_ERROR: return "ADS_ERROR";
    case MF35X_OP_SHORT: return "SHORT";
    case MF35X_OP_OPEN: return "OPEN";
    case MF35X_OP_OUT_OF_RANGE: return "OUT_OF_RANGE";
    default: return "INVALID";
  }
}

int16_t mf35xDiagRawAdc() {
  if (!isfinite(oilPressureVoltage)) return 0;
  long raw = lroundf(oilPressureVoltage / MF35X_DIAG_ADS_LSB_V);
  if (raw < 0) raw = 0;
  if (raw > INT16_MAX) raw = INT16_MAX;
  return (int16_t)raw;
}

float mf35xDiagVoltageFromRaw(int16_t raw) {
  return (float)raw * MF35X_DIAG_ADS_LSB_V;
}

float mf35xDiagOhmFromVoltage(float v) {
  if (!isfinite(v) || v <= 0.0f || v >= DRUCK_VCC - 0.001f) return NAN;
  return MF35X_DIAG_PRESSURE_FIXED_OHM * v / (DRUCK_VCC - v);
}

uint8_t mf35xDiagStateNow() {
  if (!adsOk) return MF35X_OP_ADS_ERROR;
  if (!isfinite(oilPressureVoltage)) return MF35X_OP_INVALID;
  if (oilPressureVoltage < 0.02f) return MF35X_OP_SHORT;
  if (oilPressureVoltage > 2.50f) return MF35X_OP_OPEN;

  const float ohm = mf35xDiagOhmFromVoltage(oilPressureVoltage);
  if (!isfinite(ohm)) return MF35X_OP_INVALID;
  if (ohm < 5.0f || ohm > 250.0f) return MF35X_OP_OUT_OF_RANGE;
  return MF35X_OP_OK;
}

String mf35xDiagSampleId(uint32_t bootId, uint32_t sequence) {
  char id[32];
  snprintf(id, sizeof(id), "b%08lx_s%08lx",
           (unsigned long)bootId, (unsigned long)sequence);
  return String(id);
}

String mf35xDiagQueuePath(const String& raceId) {
  return String("/dq_") + raceId + ".bin";
}

String mf35xDiagRaceIdFromPath(String path) {
  if (!path.startsWith("/")) path = "/" + path;
  if (!path.startsWith("/dq_") || !path.endsWith(".bin")) return "";
  return path.substring(4, path.length() - 4);
}

void mf35xDiagStatsReset(const String& raceId) {
  mf35xDiagRaceId = raceId;
  mf35xDiagMinBar = NAN;
  mf35xDiagMaxBar = NAN;
  mf35xDiagSumBar = 0.0;
  mf35xDiagCount = 0;
  mf35xDiagLastPersistMs = 0;
}

void mf35xDiagStatsLoad() {
  if (!preferencesOk) return;
  const String savedRace = preferences.getString("opd_rid", "");
  if (!recordingConfig.enabled || recordingConfig.raceId.length() == 0 ||
      savedRace != recordingConfig.raceId) {
    mf35xDiagStatsReset(recordingConfig.enabled ? recordingConfig.raceId : "");
    return;
  }

  mf35xDiagRaceId = savedRace;
  mf35xDiagMinBar = preferences.getFloat("opd_min", NAN);
  mf35xDiagMaxBar = preferences.getFloat("opd_max", NAN);
  mf35xDiagSumBar = preferences.getDouble("opd_sum", 0.0);
  mf35xDiagCount = preferences.getULong("opd_cnt", 0UL);

  if (mf35xDiagCount == 0 || !isfinite(mf35xDiagMinBar) ||
      !isfinite(mf35xDiagMaxBar) || !isfinite(mf35xDiagSumBar)) {
    mf35xDiagStatsReset(savedRace);
  }
}

void mf35xDiagStatsPersist(bool force = false) {
  if (!preferencesOk || mf35xDiagRaceId.length() == 0) return;
  const unsigned long now = millis();
  if (!force && (unsigned long)(now - mf35xDiagLastPersistMs) < MF35X_DIAG_STATS_PERSIST_MS) return;

  preferences.putString("opd_rid", mf35xDiagRaceId);
  preferences.putFloat("opd_min", mf35xDiagMinBar);
  preferences.putFloat("opd_max", mf35xDiagMaxBar);
  preferences.putDouble("opd_sum", mf35xDiagSumBar);
  preferences.putULong("opd_cnt", mf35xDiagCount);
  mf35xDiagLastPersistMs = now;
}

void mf35xDiagStatsAdd(float bar) {
  if (!isfinite(bar)) return;
  if (mf35xDiagCount == 0) {
    mf35xDiagMinBar = bar;
    mf35xDiagMaxBar = bar;
  } else {
    if (bar < mf35xDiagMinBar) mf35xDiagMinBar = bar;
    if (bar > mf35xDiagMaxBar) mf35xDiagMaxBar = bar;
  }
  mf35xDiagSumBar += bar;
  mf35xDiagCount++;
  mf35xDiagStatsPersist(false);
}

String mf35xDiagJson(const Mf35xOilDiagRecord& rec) {
  const float v = mf35xDiagVoltageFromRaw(rec.rawAdc);
  const float ohm = mf35xDiagOhmFromVoltage(v);
  const float rawBar = isfinite(ohm) ? widerstandZuBar(ohm) : NAN;

  String json = "{";
  json.reserve(420);
  bool first = true;
  jsonLongFeld(json, first, "oil_pressure_raw_adc", rec.rawAdc);
  jsonFloatFeld(json, first, "oil_pressure_mv", v * 1000.0f, 2);
  if (isfinite(ohm)) jsonFloatFeld(json, first, "oil_pressure_ohm", ohm, 2);
  else jsonRaw(json, first, "oil_pressure_ohm", "null");
  if (isfinite(rawBar)) jsonFloatFeld(json, first, "oil_pressure_raw_bar", rawBar, 3);
  else jsonRaw(json, first, "oil_pressure_raw_bar", "null");
  jsonText(json, first, "oil_pressure_diag", mf35xDiagStateText(rec.diagState));
  jsonFloatFeld(json, first, "oil_pressure_fixed_resistor_ohm", MF35X_DIAG_PRESSURE_FIXED_OHM, 1);
  jsonBoolFeld(json, first, "gpio11", rec.gpio11 != 0);
  json += '}';
  return json;
}

bool mf35xDiagPatchSample(const String& raceId, const Mf35xOilDiagRecord& rec, bool requireBaseSample) {
  if (WiFi.status() != WL_CONNECTED || !offlineRaceIdGueltig(raceId)) return false;

  String path = "tracker/races/" + raceId + "/samples/" +
                mf35xDiagSampleId(rec.bootId, rec.sequence);

  if (requireBaseSample) {
    String existing;
    if (!firebaseGet(path, existing)) return false;
    if (existing == "null" || existing.indexOf("\"oil_pressure\"") < 0) return false;
  }

  return firebasePatch(path, mf35xDiagJson(rec), false);
}

bool mf35xDiagQueueAppend(const String& raceId, const Mf35xOilDiagRecord& rec) {
  if (!offlineBufferReady || !offlineRaceIdGueltig(raceId)) return false;

  offlineFsStatusAktualisieren();
  const size_t freeBytes = offlineFsTotalBytes > offlineFsUsedBytes
    ? offlineFsTotalBytes - offlineFsUsedBytes
    : 0;

  // Die normale 64-Byte-Rennqueue hat immer Vorrang. Diagnosepunkte werden
  // verworfen, bevor sie den fuer die Basisdaten benoetigten Flash verbrauchen.
  if (freeBytes <= MF35X_DIAG_FLASH_PROTECT_BYTES + sizeof(rec)) {
    mf35xDiagDropped++;
    return false;
  }

  File f = LittleFS.open(mf35xDiagQueuePath(raceId), "a");
  if (!f) {
    mf35xDiagDropped++;
    return false;
  }
  const size_t written = f.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(rec));
  f.flush();
  f.close();
  if (written != sizeof(rec)) {
    mf35xDiagDropped++;
    return false;
  }
  mf35xDiagQueued++;
  return true;
}

bool mf35xDiagMarkSent(File& f, size_t offset) {
  if (!f.seek(offset + offsetof(Mf35xOilDiagRecord, state), SeekSet)) return false;
  const uint8_t sent = MF35X_DIAG_SENT;
  if (f.write(&sent, 1) != 1) return false;
  f.flush();
  return true;
}

bool mf35xDiagFileHasPending(const String& path) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  Mf35xOilDiagRecord rec;
  while (f.available() >= (int)sizeof(rec)) {
    if (f.read(reinterpret_cast<uint8_t*>(&rec), sizeof(rec)) != sizeof(rec)) break;
    if (mf35xDiagRecordValid(rec) && rec.state != MF35X_DIAG_SENT) {
      f.close();
      return true;
    }
  }
  f.close();
  return false;
}

void mf35xDiagDrainOne() {
  if (!offlineBufferReady || WiFi.status() != WL_CONNECTED) return;
  const unsigned long now = millis();
  if ((unsigned long)(now - mf35xDiagLastDrainMs) < MF35X_DIAG_DRAIN_MS) return;
  mf35xDiagLastDrainMs = now;

  File root = LittleFS.open("/");
  if (!root) return;

  String path = "";
  File e = root.openNextFile();
  while (e) {
    String n = e.name();
    e.close();
    if (!n.startsWith("/")) n = "/" + n;
    if (mf35xDiagRaceIdFromPath(n).length() > 0 && mf35xDiagFileHasPending(n)) {
      path = n;
      break;
    }
    e = root.openNextFile();
  }
  root.close();
  if (path.length() == 0) return;

  const String raceId = mf35xDiagRaceIdFromPath(path);
  File f = LittleFS.open(path, "r+");
  if (!f) return;

  size_t offset = 0;
  Mf35xOilDiagRecord rec;
  bool handled = false;
  while (f.available() >= (int)sizeof(rec)) {
    if (f.read(reinterpret_cast<uint8_t*>(&rec), sizeof(rec)) != sizeof(rec)) break;
    if (mf35xDiagRecordValid(rec) && rec.state != MF35X_DIAG_SENT) {
      if (mf35xDiagPatchSample(raceId, rec, true)) {
        if (mf35xDiagMarkSent(f, offset)) mf35xDiagReplayed++;
      }
      handled = true;
      break;
    }
    offset += sizeof(rec);
  }
  f.close();

  if (handled && !mf35xDiagFileHasPending(path)) {
    LittleFS.remove(path);
  }
}

void mf35xDiagStatsUpload() {
  if (WiFi.status() != WL_CONNECTED || mf35xDiagRaceId.length() == 0 || mf35xDiagCount == 0) return;

  String json = "{";
  json.reserve(260);
  bool first = true;
  jsonFloatFeld(json, first, "min_bar", mf35xDiagMinBar, 3);
  jsonFloatFeld(json, first, "max_bar", mf35xDiagMaxBar, 3);
  jsonFloatFeld(json, first, "avg_bar", (float)(mf35xDiagSumBar / (double)mf35xDiagCount), 3);
  jsonULongFeld(json, first, "samples", mf35xDiagCount);
  jsonText(json, first, "source", "ESP32");
  json += '}';

  firebasePatch("tracker/races/" + mf35xDiagRaceId + "/oilPressureStats", json, false);
}

void mf35xDiagCapture(uint32_t sequence) {
  if (!recordingConfig.enabled || recordingConfig.raceId.length() == 0 || sequence == 0) return;

  if (mf35xDiagRaceId != recordingConfig.raceId) {
    mf35xDiagStatsReset(recordingConfig.raceId);
  }

  mf35xDiagStatsAdd(oilPressureBar);

  Mf35xOilDiagRecord rec = {};
  rec.magic = MF35X_DIAG_MAGIC;
  rec.version = MF35X_DIAG_VERSION;
  rec.size = sizeof(rec);
  rec.bootId = offlineBootId;
  rec.sequence = sequence;
  rec.rawAdc = mf35xDiagRawAdc();
  rec.diagState = mf35xDiagStateNow();
  rec.gpio11 = schaltausgangAktiv ? 1 : 0;
  rec.state = MF35X_DIAG_PENDING;
  rec.crc32 = mf35xDiagRecordCrc(rec);

  if (!mf35xDiagPatchSample(recordingConfig.raceId, rec, false)) {
    // Offline/Fehler: nur jeden zweiten Diagnosepunkt puffern. Die Basis-Rennqueue
    // bleibt vollstaendig und hat Flash-Prioritaet; bei 5-s-Rennintervall liegen
    // dadurch auch ohne Netz etwa alle 10 s Rohdiagnosedaten vor.
    if ((sequence & 1U) == 0U) {
      mf35xDiagQueueAppend(recordingConfig.raceId, rec);
    }
  }

  mf35xDiagStatsUpload();
}

void mf35xMaxInvalidate(uint16_t bit, float& value) {
  if ((deviceMaxValues.validMask & bit) == 0) return;
  deviceMaxValues.validMask &= ~bit;
  value = NAN;
  deviceMaxDirty = true;
  deviceMaxNvsDirty = true;
}

void mf35xMaxPlausibilisieren() {
  if ((deviceMaxValues.validMask & DEVICE_MAX_SPEED_VALID) &&
      (!isfinite(deviceMaxValues.maxSpeed) || deviceMaxValues.maxSpeed < 0.0f || deviceMaxValues.maxSpeed > 130.0f)) {
    mf35xMaxInvalidate(DEVICE_MAX_SPEED_VALID, deviceMaxValues.maxSpeed);
  }
  if ((deviceMaxValues.validMask & DEVICE_MAX_RPM_VALID) &&
      (!isfinite(deviceMaxValues.maxRpm) || deviceMaxValues.maxRpm < 0.0f || deviceMaxValues.maxRpm > 5000.0f)) {
    mf35xMaxInvalidate(DEVICE_MAX_RPM_VALID, deviceMaxValues.maxRpm);
  }
  if ((deviceMaxValues.validMask & DEVICE_MAX_OIL_TEMP_VALID) &&
      (!isfinite(deviceMaxValues.maxOilTemp) || deviceMaxValues.maxOilTemp < -40.0f || deviceMaxValues.maxOilTemp > 180.0f)) {
    mf35xMaxInvalidate(DEVICE_MAX_OIL_TEMP_VALID, deviceMaxValues.maxOilTemp);
  }
  if ((deviceMaxValues.validMask & DEVICE_MAX_CYL_TEMP_VALID) &&
      (!isfinite(deviceMaxValues.maxCylTemp) || deviceMaxValues.maxCylTemp < -40.0f || deviceMaxValues.maxCylTemp > 350.0f)) {
    mf35xMaxInvalidate(DEVICE_MAX_CYL_TEMP_VALID, deviceMaxValues.maxCylTemp);
  }
  if ((deviceMaxValues.validMask & DEVICE_MIN_OIL_PRESSURE_VALID) &&
      (!isfinite(deviceMaxValues.minOilPressure) || deviceMaxValues.minOilPressure < 0.0f || deviceMaxValues.minOilPressure > 12.0f)) {
    mf35xMaxInvalidate(DEVICE_MIN_OIL_PRESSURE_VALID, deviceMaxValues.minOilPressure);
  }
  if ((deviceMaxValues.validMask & DEVICE_MIN_BATTERY_VALID) &&
      (!isfinite(deviceMaxValues.minBattery) || deviceMaxValues.minBattery < 6.0f || deviceMaxValues.minBattery > 18.0f)) {
    mf35xMaxInvalidate(DEVICE_MIN_BATTERY_VALID, deviceMaxValues.minBattery);
  }
}

void mf35xV5917PatchSetup() {
  // Ab V5.9.17 ist NVS/ESP32 die verbindliche Quelle. Damit koennen nach einem
  // Admin-Reset keine alten Browser-/Firebase-Maximalwerte mehr zurueckkommen.
  deviceMaxFirebaseMerged = true;
  mf35xMaxPlausibilisieren();
  deviceMaxDirty = true;      // lokalen Stand einmal nach Firebase spiegeln
  deviceMaxNvsDirty = true;
  deviceMaxSpeichern(true);
  mf35xDiagStatsLoad();

  Serial.println("V5.9.17 Patch: Max-Reset robust + Oeldruck-Rohdiagnose aktiv");
}

void mf35xV5917PatchLoop(uint32_t raceSequenceBefore) {
  mf35xMaxPlausibilisieren();

  if (!recordingConfig.enabled || recordingConfig.raceId.length() == 0) {
    mf35xDiagStatsPersist(false);
  } else if (offlineSampleSequence != raceSequenceBefore) {
    // rennhistorieBearbeiten() erzeugt pro Faelligkeit genau einen neuen Sample.
    mf35xDiagCapture(offlineSampleSequence);
  }

  mf35xDiagDrainOne();
}
