from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TARGET_VERSION = "V5.9.17"
TARGET_VERSION_CODE = "50917UL"


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def write(rel, text):
    (ROOT / rel).write_text(text, encoding="utf-8")


def replace_exact(rel, old, new, count=1):
    text = read(rel)
    found = text.count(old)
    if found != count:
        raise SystemExit(f"{rel}: expected {count} occurrence(s), found {found}: {old[:100]!r}")
    write(rel, text.replace(old, new, count))


def already_prepared():
    version = read("esp32/MF35X_Livetracker/firmware_version.h")
    admin_html = read("admin.html")
    diag = ROOT / "esp32/MF35X_Livetracker/oil_pressure_diagnostics.hpp"
    return (
        TARGET_VERSION in version
        and "admin-reset-fix.js" not in admin_html
        and diag.exists()
        and "DEVICE_MAX_STATE_PENDING_BIT" in read("esp32/MF35X_Livetracker/device_metrics_alarm.hpp")
        and "oil_pressure_diag_status" in read("admin.js")
    )


if already_prepared():
    print("Prepared firmware already present; nothing to change.")
    raise SystemExit(0)

# -----------------------------------------------------------------------------
# 1) Website: one reset implementation only, consistent defaults, richer CSV.
# -----------------------------------------------------------------------------
replace_exact(
    "admin.js",
    "/* MF35X Tracker Admin V9.5.14 */",
    "/* MF35X Tracker Admin V9.5.15 */",
)
replace_exact(
    "admin.js",
    "const DEFAULT_OUTPUT_CONFIG = {\n  speed_enable_kmh: 60,\n  rpm_on: 3200,\n  rpm_off: 3150\n};",
    "const DEFAULT_OUTPUT_CONFIG = {\n  speed_enable_kmh: 60,\n  rpm_on: 2500,\n  rpm_off: 2450\n};",
)
replace_exact(
    "admin.js",
    '''function analysisNormalizeSample(sample) {
  const normalized = { timestamp: Number(sample.timestamp) };
  for (const key of Object.keys(ANALYSIS_METRICS)) {
    const n = Number(sample[key]);
    normalized[key] = Number.isFinite(n) ? n : null;
  }
  return normalized;
}''',
    '''function analysisNormalizeNumber(value) {
  if (value === undefined || value === null || value === "") return null;
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}

function analysisNormalizeSample(sample) {
  const normalized = { timestamp: Number(sample.timestamp) };
  for (const key of Object.keys(ANALYSIS_METRICS)) {
    normalized[key] = analysisNormalizeNumber(sample[key]);
  }

  // Diagnose-/Kontextwerte werden bewusst nicht in die normale Besucheranzeige
  // aufgenommen. Sie stehen nur der Admin-Rennauswertung und dem CSV-Export zur Verfügung.
  for (const key of [
    "battery_v", "hdop", "satellites", "wifi_rssi",
    "oil_pressure_adc_avg", "oil_pressure_adc_min", "oil_pressure_adc_max",
    "oil_pressure_voltage_avg", "oil_pressure_voltage_min", "oil_pressure_voltage_max",
    "oil_pressure_ohm", "oil_pressure_raw_bar",
    "oil_pressure_diag_samples", "oil_pressure_diag_invalid"
  ]) {
    normalized[key] = analysisNormalizeNumber(sample[key]);
  }

  normalized.oil_pressure_diag_status =
    typeof sample.oil_pressure_diag_status === "string"
      ? sample.oil_pressure_diag_status
      : "";
  normalized.sample_id = typeof sample.sample_id === "string" ? sample.sample_id : "";
  normalized.buffered_replay = sample.buffered_replay === true;
  return normalized;
}'''
)
replace_exact(
    "admin.js",
    '''  const header = [
    "Zeitpunkt", "timestamp", "Zylinderkopftemperatur_C", "Motoroeltemperatur_C",
    "Getriebeoeltemperatur_C", "Oeldruck_bar", "Drehzahl_Umin", "Geschwindigkeit_kmh"
  ];
  const rows = analysisCurrentSamples.map(s => [
    analysisFormatDateTime(s.timestamp, true), s.timestamp,
    analysisCsvNumber(s.cylinder_temp), analysisCsvNumber(s.oil_temp),
    analysisCsvNumber(s.gear_oil_temp), analysisCsvNumber(s.oil_pressure),
    analysisCsvNumber(s.rpm), analysisCsvNumber(s.speed_kmh)
  ]);''',
    '''  const header = [
    "Zeitpunkt", "timestamp", "Zylinderkopftemperatur_C", "Motoroeltemperatur_C",
    "Getriebeoeltemperatur_C", "Oeldruck_final_bar", "Drehzahl_Umin", "Geschwindigkeit_kmh",
    "Batterie_V", "GPS_HDOP", "GPS_Satelliten", "WLAN_RSSI_dBm",
    "Oeldruck_AIN1_ADC_Mittel", "Oeldruck_AIN1_ADC_Min", "Oeldruck_AIN1_ADC_Max",
    "Oeldruck_AIN1_Spannung_Mittel_V", "Oeldruck_AIN1_Spannung_Min_V", "Oeldruck_AIN1_Spannung_Max_V",
    "Oeldruck_Geber_Ohm", "Oeldruck_vor_Begrenzung_bar",
    "Oeldruck_Diagnose_Messungen", "Oeldruck_Diagnose_Ungueltig", "Oeldruck_Diagnose_Status",
    "Sample_ID", "Offline_nachgesendet"
  ];
  const rows = analysisCurrentSamples.map(s => [
    analysisFormatDateTime(s.timestamp, true), s.timestamp,
    analysisCsvNumber(s.cylinder_temp), analysisCsvNumber(s.oil_temp),
    analysisCsvNumber(s.gear_oil_temp), analysisCsvNumber(s.oil_pressure),
    analysisCsvNumber(s.rpm), analysisCsvNumber(s.speed_kmh),
    analysisCsvNumber(s.battery_v), analysisCsvNumber(s.hdop),
    analysisCsvNumber(s.satellites), analysisCsvNumber(s.wifi_rssi),
    analysisCsvNumber(s.oil_pressure_adc_avg), analysisCsvNumber(s.oil_pressure_adc_min),
    analysisCsvNumber(s.oil_pressure_adc_max), analysisCsvNumber(s.oil_pressure_voltage_avg),
    analysisCsvNumber(s.oil_pressure_voltage_min), analysisCsvNumber(s.oil_pressure_voltage_max),
    analysisCsvNumber(s.oil_pressure_ohm), analysisCsvNumber(s.oil_pressure_raw_bar),
    analysisCsvNumber(s.oil_pressure_diag_samples), analysisCsvNumber(s.oil_pressure_diag_invalid),
    s.oil_pressure_diag_status || "", s.sample_id || "", s.buffered_replay ? "JA" : "NEIN"
  ]);'''
)

replace_exact(
    "admin.html",
    "<title>MF35X Admin V9.5.13</title>",
    "<title>MF35X Admin V9.5.15</title>",
)
replace_exact(
    "admin.html",
    '<div class="version">Version 9.5.12</div>',
    '<div class="version">Version 9.5.15</div>',
)
replace_exact(
    "admin.html",
    '  await import(`./admin.js?v=9.5.13-${Date.now()}`);\n  await import(`./admin-reset-fix.js?v=9.5.13-${Date.now()}`);',
    '  await import(`./admin.js?v=9.5.15-${Date.now()}`);',
)
replace_exact(
    "admin.html",
    '  const { firebaseConfig } = await import(`./firebase-config.js?v=9.5.13-${Date.now()}`);',
    '  const { firebaseConfig } = await import(`./firebase-config.js?v=9.5.15-${Date.now()}`);',
)
legacy_reset = ROOT / "admin-reset-fix.js"
if legacy_reset.exists():
    legacy_reset.unlink()

# -----------------------------------------------------------------------------
# 2) Firmware version and consistent fallback configuration.
# -----------------------------------------------------------------------------
write(
    "esp32/MF35X_Livetracker/firmware_version.h",
    '#pragma once\n\n#define MF35X_FIRMWARE_VERSION "V5.9.17"\n#define MF35X_FIRMWARE_VERSION_CODE 50917UL\n',
)

core = "esp32/MF35X_Livetracker/MF35X_Livetracker_core.hpp"
replace_exact(
    core,
    '''OutputConfig outputConfig = {
  60.0f,
  3200.0f,
  3150.0f
};''',
    '''OutputConfig outputConfig = {
  60.0f,
  2500.0f,
  2450.0f
};'''
)
replace_exact(
    core,
    "    outputConfig = {60.0f, 3200.0f, 3150.0f};",
    "    outputConfig = {60.0f, 2500.0f, 2450.0f};",
)
replace_exact(
    core,
    '  jsonText(json, first, "firmware", "V5.9.7");',
    '  jsonText(json, first, "firmware", MF35X_FIRMWARE_VERSION);',
)
replace_exact(
    core,
    '  Serial.println("MF35X LIVETRACKER V5.9.13 OTA SIGNED");',
    '  Serial.println("MF35X LIVETRACKER V5.9.17 OTA SIGNED");',
)

# Oil-pressure globals retain final value but also expose pre-clamp/raw diagnosis.
replace_exact(
    core,
    '''float oilPressureVoltage = NAN;
float oilPressureOhm = NAN;
float oilPressureBar = NAN;''',
    '''float oilPressureVoltage = NAN;
float oilPressureOhm = NAN;
float oilPressureBarRaw = NAN;
float oilPressureBar = NAN;
int16_t oilPressureAdcAvg = 0;
int16_t oilPressureAdcMin = 0;
int16_t oilPressureAdcMax = 0;
uint8_t oilPressureDiagStatus = MF35X_OIL_DIAG_NO_DATA;'''
)

old_oil_reader = '''void oeldruckLesen() {
  if (!adsOk) {
    oilPressureVoltage = NAN;
    oilPressureOhm = NAN;
    oilPressureBar = NAN;
    return;
  }

  float summe = 0.0f;

  for (int i = 0; i < 20; i++) {
    int16_t rohwert = ads.readADC_SingleEnded(ADS_KANAL_OELDRUCK);
    summe += ads.computeVolts(rohwert);
    delay(5);
    gpsEinlesen();
  }

  oilPressureVoltage = summe / 20.0f;

  if (oilPressureVoltage < 0.02f) {
    oilPressureOhm = NAN;
    oilPressureBar = NAN;
    return;
  }

  if (oilPressureVoltage > 3.10f ||
      oilPressureVoltage >= DRUCK_VCC - 0.02f) {
    oilPressureOhm = NAN;
    oilPressureBar = NAN;
    return;
  }

  oilPressureOhm =
    DRUCK_R_FIXED *
    oilPressureVoltage /
    (DRUCK_VCC - oilPressureVoltage);

  oilPressureBar = widerstandZuBar(oilPressureOhm);

  if (!isnan(oilPressureBar) && oilPressureBar < 0.55f) {
    oilPressureBar = 0.0f;
  }
}'''
new_oil_reader = '''void oeldruckLesen() {
  if (!adsOk) {
    oilPressureVoltage = NAN;
    oilPressureOhm = NAN;
    oilPressureBarRaw = NAN;
    oilPressureBar = NAN;
    oilPressureAdcAvg = 0;
    oilPressureAdcMin = 0;
    oilPressureAdcMax = 0;
    oilPressureDiagStatus = MF35X_OIL_DIAG_ADS_OFFLINE;
    mf35xOilDiagObserve(false, false, 0, 0, 0, NAN, NAN, NAN, NAN, oilPressureDiagStatus);
    return;
  }

  float summe = 0.0f;
  int32_t rohSumme = 0;
  int16_t rohMin = INT16_MAX;
  int16_t rohMax = INT16_MIN;

  for (int i = 0; i < 20; i++) {
    const int16_t rohwert = ads.readADC_SingleEnded(ADS_KANAL_OELDRUCK);
    rohSumme += rohwert;
    if (rohwert < rohMin) rohMin = rohwert;
    if (rohwert > rohMax) rohMax = rohwert;
    summe += ads.computeVolts(rohwert);
    delay(5);
    gpsEinlesen();
  }

  oilPressureAdcAvg = (int16_t)lroundf((float)rohSumme / 20.0f);
  oilPressureAdcMin = rohMin;
  oilPressureAdcMax = rohMax;
  oilPressureVoltage = summe / 20.0f;
  oilPressureOhm = NAN;
  oilPressureBarRaw = NAN;
  oilPressureBar = NAN;

  if (oilPressureVoltage < 0.02f) {
    oilPressureDiagStatus = MF35X_OIL_DIAG_SHORT_OR_LOW;
    mf35xOilDiagObserve(true, true, oilPressureAdcAvg, oilPressureAdcMin, oilPressureAdcMax,
                        oilPressureVoltage, NAN, NAN, NAN, oilPressureDiagStatus);
    return;
  }

  if (oilPressureVoltage > 3.10f ||
      oilPressureVoltage >= DRUCK_VCC - 0.02f) {
    oilPressureDiagStatus = MF35X_OIL_DIAG_OPEN_OR_HIGH;
    mf35xOilDiagObserve(true, true, oilPressureAdcAvg, oilPressureAdcMin, oilPressureAdcMax,
                        oilPressureVoltage, NAN, NAN, NAN, oilPressureDiagStatus);
    return;
  }

  oilPressureOhm =
    DRUCK_R_FIXED *
    oilPressureVoltage /
    (DRUCK_VCC - oilPressureVoltage);

  oilPressureBarRaw = widerstandZuBar(oilPressureOhm);
  oilPressureBar = oilPressureBarRaw;
  oilPressureDiagStatus = MF35X_OIL_DIAG_OK;

  if (!isnan(oilPressureBar) && oilPressureBar < 0.55f) {
    oilPressureBar = 0.0f;
    oilPressureDiagStatus = MF35X_OIL_DIAG_CLAMP_ZERO;
  }

  if (!isfinite(oilPressureOhm) || !isfinite(oilPressureBarRaw)) {
    oilPressureDiagStatus = MF35X_OIL_DIAG_INVALID;
  }

  mf35xOilDiagObserve(true, true, oilPressureAdcAvg, oilPressureAdcMin, oilPressureAdcMax,
                      oilPressureVoltage, oilPressureOhm, oilPressureBarRaw,
                      oilPressureBar, oilPressureDiagStatus);
}'''
replace_exact(core, old_oil_reader, new_oil_reader)

replace_exact(
    core,
    '''  if (oilDue) {
    jsonFloatFeld(json, first, "oil_pressure", oilPressureBar, 2);
    jsonFloatFeld(json, first, "oil_pressure_voltage", oilPressureVoltage, 4);
    jsonFloatFeld(json, first, "oil_pressure_ohm", oilPressureOhm, 1);
  }''',
    '''  if (oilDue) {
    jsonFloatFeld(json, first, "oil_pressure", oilPressureBar, 2);
    jsonFloatFeld(json, first, "oil_pressure_raw_bar", oilPressureBarRaw, 2);
    jsonFloatFeld(json, first, "oil_pressure_voltage", oilPressureVoltage, 4);
    jsonFloatFeld(json, first, "oil_pressure_ohm", oilPressureOhm, 1);
    jsonRaw(json, first, "oil_pressure_adc_avg", String(oilPressureAdcAvg));
    jsonRaw(json, first, "oil_pressure_adc_min", String(oilPressureAdcMin));
    jsonRaw(json, first, "oil_pressure_adc_max", String(oilPressureAdcMax));
    jsonText(json, first, "oil_pressure_diag_status", mf35xOilDiagStatusText(oilPressureDiagStatus));
  }'''
)
replace_exact(
    core,
    '  jsonBoolFeld(json, first, "historySupported", true);',
    '  jsonBoolFeld(json, first, "historySupported", true);\n  jsonBoolFeld(json, first, "oilPressureDiagnosticsSupported", true);',
)

# -----------------------------------------------------------------------------
# 3) Max-value hardening: generation barrier + power-fail-safe reset + filters.
# -----------------------------------------------------------------------------
metrics = "esp32/MF35X_Livetracker/device_metrics_alarm.hpp"
replace_exact(
    metrics,
    '''constexpr float DEVICE_OIL_PRESSURE_RPM_MIN = 400.0f;
constexpr unsigned long DEVICE_OIL_PRESSURE_START_DELAY_MS = 5000UL;''',
    '''constexpr float DEVICE_OIL_PRESSURE_RPM_MIN = 400.0f;
constexpr unsigned long DEVICE_OIL_PRESSURE_START_DELAY_MS = 5000UL;
// Last-line plausibility guards for stored maxima only. Live values remain untouched.
constexpr float DEVICE_MAX_SPEED_PLAUSIBLE_KMH = 120.0f;
constexpr float DEVICE_MAX_RPM_PLAUSIBLE = 6000.0f;
// mx_state: high bit = reset transaction pending, low 31 bits = generation.
constexpr uint32_t DEVICE_MAX_STATE_PENDING_BIT = 0x80000000UL;
constexpr uint32_t DEVICE_MAX_STATE_GENERATION_MASK = 0x7FFFFFFFUL;'''
)
replace_exact(
    metrics,
    '''unsigned long deviceMaxLastMergeAttemptMs = 0;
unsigned long deviceOilPressureEngineStartAt = 0;''',
    '''unsigned long deviceMaxLastMergeAttemptMs = 0;
unsigned long deviceOilPressureEngineStartAt = 0;
uint32_t deviceMaxGeneration = 0;
bool deviceMaxRecoveredInterruptedReset = false;'''
)

old_load = '''void deviceMaxLaden() {
  if (!preferencesOk) return;

  deviceMaxValues.validMask = (uint16_t)preferences.getUInt("mx_mask", 0U);
  deviceMaxValues.maxSpeed = preferences.getFloat("mx_spd", NAN);
  deviceMaxValues.maxRpm = preferences.getFloat("mx_rpm", NAN);
  deviceMaxValues.maxOilTemp = preferences.getFloat("mx_ot", NAN);
  deviceMaxValues.maxCylTemp = preferences.getFloat("mx_ct", NAN);
  deviceMaxValues.minOilPressure = preferences.getFloat("mn_op", NAN);
  deviceMaxValues.minBattery = preferences.getFloat("mn_bat", NAN);

  deviceAlarmSequence = preferences.getULong("alarm_seq", 0UL);
  deviceAlarmActiveMask = (uint16_t)preferences.getUInt("alarm_mask", 0U);
  deviceLastMaxResetCommandId = preferences.getString("cmd_max", "");
  deviceLastAlarmClearCommandId = preferences.getString("cmd_ahclr", "");

  if ((deviceMaxValues.validMask & DEVICE_MAX_SPEED_VALID) && !isfinite(deviceMaxValues.maxSpeed)) deviceMaxValues.validMask &= ~DEVICE_MAX_SPEED_VALID;
  if ((deviceMaxValues.validMask & DEVICE_MAX_RPM_VALID) && !isfinite(deviceMaxValues.maxRpm)) deviceMaxValues.validMask &= ~DEVICE_MAX_RPM_VALID;
  if ((deviceMaxValues.validMask & DEVICE_MAX_OIL_TEMP_VALID) && !isfinite(deviceMaxValues.maxOilTemp)) deviceMaxValues.validMask &= ~DEVICE_MAX_OIL_TEMP_VALID;
  if ((deviceMaxValues.validMask & DEVICE_MAX_CYL_TEMP_VALID) && !isfinite(deviceMaxValues.maxCylTemp)) deviceMaxValues.validMask &= ~DEVICE_MAX_CYL_TEMP_VALID;
  if ((deviceMaxValues.validMask & DEVICE_MIN_OIL_PRESSURE_VALID) && !isfinite(deviceMaxValues.minOilPressure)) deviceMaxValues.validMask &= ~DEVICE_MIN_OIL_PRESSURE_VALID;
  if ((deviceMaxValues.validMask & DEVICE_MIN_BATTERY_VALID) && !isfinite(deviceMaxValues.minBattery)) deviceMaxValues.validMask &= ~DEVICE_MIN_BATTERY_VALID;
}'''
new_load = '''void deviceMaxLaden() {
  if (!preferencesOk) return;

  const uint32_t state = preferences.getUInt("mx_state", 0U);
  deviceMaxGeneration = state & DEVICE_MAX_STATE_GENERATION_MASK;
  const bool resetPending = (state & DEVICE_MAX_STATE_PENDING_BIT) != 0;

  deviceMaxValues.validMask = (uint16_t)preferences.getUInt("mx_mask", 0U);
  deviceMaxValues.maxSpeed = preferences.getFloat("mx_spd", NAN);
  deviceMaxValues.maxRpm = preferences.getFloat("mx_rpm", NAN);
  deviceMaxValues.maxOilTemp = preferences.getFloat("mx_ot", NAN);
  deviceMaxValues.maxCylTemp = preferences.getFloat("mx_ct", NAN);
  deviceMaxValues.minOilPressure = preferences.getFloat("mn_op", NAN);
  deviceMaxValues.minBattery = preferences.getFloat("mn_bat", NAN);

  deviceAlarmSequence = preferences.getULong("alarm_seq", 0UL);
  deviceAlarmActiveMask = (uint16_t)preferences.getUInt("alarm_mask", 0U);
  deviceLastMaxResetCommandId = preferences.getString("cmd_max", "");
  deviceLastAlarmClearCommandId = preferences.getString("cmd_ahclr", "");

  if ((deviceMaxValues.validMask & DEVICE_MAX_SPEED_VALID) && !isfinite(deviceMaxValues.maxSpeed)) deviceMaxValues.validMask &= ~DEVICE_MAX_SPEED_VALID;
  if ((deviceMaxValues.validMask & DEVICE_MAX_RPM_VALID) && !isfinite(deviceMaxValues.maxRpm)) deviceMaxValues.validMask &= ~DEVICE_MAX_RPM_VALID;
  if ((deviceMaxValues.validMask & DEVICE_MAX_OIL_TEMP_VALID) && !isfinite(deviceMaxValues.maxOilTemp)) deviceMaxValues.validMask &= ~DEVICE_MAX_OIL_TEMP_VALID;
  if ((deviceMaxValues.validMask & DEVICE_MAX_CYL_TEMP_VALID) && !isfinite(deviceMaxValues.maxCylTemp)) deviceMaxValues.validMask &= ~DEVICE_MAX_CYL_TEMP_VALID;
  if ((deviceMaxValues.validMask & DEVICE_MIN_OIL_PRESSURE_VALID) && !isfinite(deviceMaxValues.minOilPressure)) deviceMaxValues.validMask &= ~DEVICE_MIN_OIL_PRESSURE_VALID;
  if ((deviceMaxValues.validMask & DEVICE_MIN_BATTERY_VALID) && !isfinite(deviceMaxValues.minBattery)) deviceMaxValues.validMask &= ~DEVICE_MIN_BATTERY_VALID;

  // A power cut after the reset transaction marker but before the value erase
  // is completed here before any Firebase merge can happen.
  if (resetPending) {
    deviceMaxValues = DeviceMaxValues{};
    preferences.putUInt("mx_mask", 0U);
    preferences.putUInt("mx_state", deviceMaxGeneration);
    deviceMaxDirty = true;
    deviceMaxNvsDirty = false;
    deviceMaxResetTimestampPending = true;
    deviceMaxRecoveredInterruptedReset = true;
  }
}'''
replace_exact(metrics, old_load, new_load)

replace_exact(
    metrics,
    '''  preferences.putFloat("mn_op", deviceMaxValues.minOilPressure);
  preferences.putFloat("mn_bat", deviceMaxValues.minBattery);
  deviceMaxLastPersistMs = now;''',
    '''  preferences.putFloat("mn_op", deviceMaxValues.minOilPressure);
  preferences.putFloat("mn_bat", deviceMaxValues.minBattery);
  // Commit generation last. A pending reset marker is therefore never cleared
  // until all value fields and the validity mask have been written.
  preferences.putUInt("mx_state", deviceMaxGeneration & DEVICE_MAX_STATE_GENERATION_MASK);
  deviceMaxLastPersistMs = now;'''
)

replace_exact(
    metrics,
    '''void deviceMaxWertSetzen(uint16_t bit, float& target, float value, bool maximum) {
  const bool valid = (deviceMaxValues.validMask & bit) != 0;''',
    '''bool deviceMaxPlausibel(uint16_t bit, float value) {
  if (!isfinite(value)) return false;
  if (bit == DEVICE_MAX_SPEED_VALID) {
    return value >= 0.0f && value <= DEVICE_MAX_SPEED_PLAUSIBLE_KMH;
  }
  if (bit == DEVICE_MAX_RPM_VALID) {
    return value >= 0.0f && value <= DEVICE_MAX_RPM_PLAUSIBLE;
  }
  return true;
}

void deviceMaxWertSetzen(uint16_t bit, float& target, float value, bool maximum) {
  if (!deviceMaxPlausibel(bit, value)) return;
  const bool valid = (deviceMaxValues.validMask & bit) != 0;'''
)

old_merge = '''void deviceMaxFirebaseMergeBearbeiten() {
  if (deviceMaxFirebaseMerged || WiFi.status() != WL_CONNECTED) return;

  const unsigned long now = millis();
  if ((unsigned long)(now - deviceMaxLastMergeAttemptMs) < DEVICE_MAX_REMOTE_MERGE_RETRY_MS) return;
  deviceMaxLastMergeAttemptMs = now;

  String json;
  if (!firebaseGet("tracker/maxValues", json)) return;

  deviceMaxMergeWert(json, "maxSpeed", DEVICE_MAX_SPEED_VALID, deviceMaxValues.maxSpeed, true);
  deviceMaxMergeWert(json, "maxRpm", DEVICE_MAX_RPM_VALID, deviceMaxValues.maxRpm, true);
  deviceMaxMergeWert(json, "maxOilTemp", DEVICE_MAX_OIL_TEMP_VALID, deviceMaxValues.maxOilTemp, true);
  deviceMaxMergeWert(json, "maxCylTemp", DEVICE_MAX_CYL_TEMP_VALID, deviceMaxValues.maxCylTemp, true);
  deviceMaxMergeWert(json, "minOilPressure", DEVICE_MIN_OIL_PRESSURE_VALID, deviceMaxValues.minOilPressure, false);
  deviceMaxMergeWert(json, "minBattery", DEVICE_MIN_BATTERY_VALID, deviceMaxValues.minBattery, false);

  deviceMaxFirebaseMerged = true;
  deviceMaxDirty = true;
}'''
new_merge = '''void deviceMaxFirebaseMergeBearbeiten() {
  if (deviceMaxFirebaseMerged || WiFi.status() != WL_CONNECTED) return;

  const unsigned long now = millis();
  if ((unsigned long)(now - deviceMaxLastMergeAttemptMs) < DEVICE_MAX_REMOTE_MERGE_RETRY_MS) return;
  deviceMaxLastMergeAttemptMs = now;

  String json;
  if (!firebaseGet("tracker/maxValues", json)) return;

  double remoteGenerationNumber = 0.0;
  const bool hasRemoteGeneration = jsonZahl(json, "generation", remoteGenerationNumber) &&
                                   isfinite(remoteGenerationNumber) &&
                                   remoteGenerationNumber >= 0.0;
  uint32_t remoteGeneration = hasRemoteGeneration
    ? ((uint32_t)remoteGenerationNumber & DEVICE_MAX_STATE_GENERATION_MASK)
    : 0U;

  // One-time migration from the legacy generation-less format.
  if (deviceMaxGeneration == 0U && remoteGeneration == 0U) {
    deviceMaxMergeWert(json, "maxSpeed", DEVICE_MAX_SPEED_VALID, deviceMaxValues.maxSpeed, true);
    deviceMaxMergeWert(json, "maxRpm", DEVICE_MAX_RPM_VALID, deviceMaxValues.maxRpm, true);
    deviceMaxMergeWert(json, "maxOilTemp", DEVICE_MAX_OIL_TEMP_VALID, deviceMaxValues.maxOilTemp, true);
    deviceMaxMergeWert(json, "maxCylTemp", DEVICE_MAX_CYL_TEMP_VALID, deviceMaxValues.maxCylTemp, true);
    deviceMaxMergeWert(json, "minOilPressure", DEVICE_MIN_OIL_PRESSURE_VALID, deviceMaxValues.minOilPressure, false);
    deviceMaxMergeWert(json, "minBattery", DEVICE_MIN_BATTERY_VALID, deviceMaxValues.minBattery, false);
    deviceMaxGeneration = 1U;
    deviceMaxNvsDirty = true;
  } else if (remoteGeneration < deviceMaxGeneration) {
    // Stale Firebase data from a generation before the last reset is never imported.
  } else {
    if (remoteGeneration > deviceMaxGeneration) {
      // A newer generation may come from the same device after an NVS rollback/replacement.
      // Start from an empty local set so values from two generations cannot mix.
      deviceMaxValues = DeviceMaxValues{};
      deviceMaxGeneration = remoteGeneration;
      deviceMaxNvsDirty = true;
    }

    deviceMaxMergeWert(json, "maxSpeed", DEVICE_MAX_SPEED_VALID, deviceMaxValues.maxSpeed, true);
    deviceMaxMergeWert(json, "maxRpm", DEVICE_MAX_RPM_VALID, deviceMaxValues.maxRpm, true);
    deviceMaxMergeWert(json, "maxOilTemp", DEVICE_MAX_OIL_TEMP_VALID, deviceMaxValues.maxOilTemp, true);
    deviceMaxMergeWert(json, "maxCylTemp", DEVICE_MAX_CYL_TEMP_VALID, deviceMaxValues.maxCylTemp, true);
    deviceMaxMergeWert(json, "minOilPressure", DEVICE_MIN_OIL_PRESSURE_VALID, deviceMaxValues.minOilPressure, false);
    deviceMaxMergeWert(json, "minBattery", DEVICE_MIN_BATTERY_VALID, deviceMaxValues.minBattery, false);
  }

  if (deviceMaxGeneration == 0U) {
    deviceMaxGeneration = 1U;
    deviceMaxNvsDirty = true;
  }

  deviceMaxFirebaseMerged = true;
  deviceMaxDirty = true;
  deviceMaxSpeichern(true);
}'''
replace_exact(metrics, old_merge, new_merge)

replace_exact(
    metrics,
    '''  deviceJsonOptionalFloat(json, first, "minBattery", DEVICE_MIN_BATTERY_VALID, deviceMaxValues.minBattery, 2);
  jsonBoolFeld(json, first, "deviceOwned", true);''',
    '''  deviceJsonOptionalFloat(json, first, "minBattery", DEVICE_MIN_BATTERY_VALID, deviceMaxValues.minBattery, 2);
  jsonULongFeld(json, first, "generation", deviceMaxGeneration);
  jsonBoolFeld(json, first, "deviceOwned", true);'''
)

old_reset = '''void deviceMaxReset() {
  deviceMaxValues = DeviceMaxValues{};
  deviceMaxDirty = true;
  deviceMaxNvsDirty = true;
  deviceMaxResetTimestampPending = true;
  deviceMaxFirebaseMerged = true;
  deviceOilPressureEngineStartAt = 0;
  deviceMaxAktualisieren();
  deviceMaxSpeichern(true);
  deviceMaxLastUploadAttemptMs = millis() - DEVICE_MAX_UPLOAD_RETRY_MS;
  deviceMaxUploadBearbeiten();
}'''
new_reset = '''void deviceMaxReset() {
  uint32_t nextGeneration = deviceMaxGeneration >= 1U ? deviceMaxGeneration + 1U : 1U;
  if (nextGeneration == 0U || nextGeneration > DEVICE_MAX_STATE_GENERATION_MASK) {
    nextGeneration = 1U;
  }

  // Transaction marker FIRST: if power disappears anywhere below, boot recovery
  // sees the pending bit and completes the erase before Firebase is considered.
  if (preferencesOk) {
    preferences.putUInt("mx_state", DEVICE_MAX_STATE_PENDING_BIT | nextGeneration);
  }

  deviceMaxGeneration = nextGeneration;
  deviceMaxValues = DeviceMaxValues{};
  deviceMaxDirty = true;
  deviceMaxNvsDirty = true;
  deviceMaxResetTimestampPending = true;
  deviceMaxFirebaseMerged = true;
  deviceMaxRecoveredInterruptedReset = false;
  deviceOilPressureEngineStartAt = 0;

  // A reset means "start a new measurement generation now", therefore the
  // currently valid values may immediately become the first values of it.
  deviceMaxAktualisieren();
  deviceMaxSpeichern(true);
  deviceMaxLastUploadAttemptMs = millis() - DEVICE_MAX_UPLOAD_RETRY_MS;
  deviceMaxUploadBearbeiten();
}'''
replace_exact(metrics, old_reset, new_reset)

# -----------------------------------------------------------------------------
# 4) Diagnostic companion record: preserves old 64-byte race queue compatibility.
# -----------------------------------------------------------------------------
diag_header = r'''#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <FS.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <limits.h>

// Oil-pressure diagnostic companion for race samples.
// IMPORTANT: the existing OfflineRaceRecord stays version 1 / 64 bytes so any
// records buffered before this OTA remain readable. Diagnostics use a separate
// keyed queue and are merged into the same deterministic Firebase PUT.

constexpr uint8_t MF35X_OIL_DIAG_NO_DATA = 0;
constexpr uint8_t MF35X_OIL_DIAG_OK = 1;
constexpr uint8_t MF35X_OIL_DIAG_CLAMP_ZERO = 2;
constexpr uint8_t MF35X_OIL_DIAG_SHORT_OR_LOW = 3;
constexpr uint8_t MF35X_OIL_DIAG_OPEN_OR_HIGH = 4;
constexpr uint8_t MF35X_OIL_DIAG_INVALID = 5;
constexpr uint8_t MF35X_OIL_DIAG_ADS_OFFLINE = 6;

constexpr uint32_t MF35X_OIL_DIAG_MAGIC = 0x4F494431UL; // OID1
constexpr uint16_t MF35X_OIL_DIAG_VERSION = 1;
constexpr uint8_t MF35X_OIL_DIAG_PENDING = 0xA5;
constexpr uint8_t MF35X_OIL_DIAG_SENT = 0x5A;
constexpr size_t MF35X_OIL_DIAG_FLASH_RESERVE = 96UL * 1024UL;
const char* MF35X_OIL_DIAG_QUEUE = "/oildq.bin";
const char* MF35X_OIL_DIAG_REPAIR = "/oildq.repair";

#pragma pack(push, 1)
struct Mf35xOilDiagRecord {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t bootId;
  uint32_t sequence;
  int16_t adcAvg;
  int16_t adcMin;
  int16_t adcMax;
  uint16_t ohmDeci;
  int16_t rawPressureCenti;
  uint16_t sampleCount;
  uint16_t rawCount;
  uint16_t invalidCount;
  uint8_t status;
  uint8_t reserved;
  uint32_t crc32;
  uint8_t state;
  uint8_t padding[3];
};
#pragma pack(pop)

static_assert(sizeof(Mf35xOilDiagRecord) == 42, "Mf35xOilDiagRecord layout changed");

struct Mf35xOilDiagWindow {
  uint32_t sampleCount = 0;
  uint32_t rawCount = 0;
  uint32_t ohmCount = 0;
  uint32_t rawBarCount = 0;
  uint32_t invalidCount = 0;
  int64_t adcSum = 0;
  int16_t adcMin = INT16_MAX;
  int16_t adcMax = INT16_MIN;
  double ohmSum = 0.0;
  double rawBarSum = 0.0;
  uint8_t status = MF35X_OIL_DIAG_NO_DATA;
};

Mf35xOilDiagWindow mf35xOilDiagWindow;
Mf35xOilDiagRecord mf35xOilDiagStaged = {};
bool mf35xOilDiagStagedValid = false;
uint32_t mf35xOilDiagQueued = 0;
uint32_t mf35xOilDiagDropped = 0;
uint32_t mf35xOilDiagCorrupt = 0;
String mf35xOilDiagLastError = "";

uint32_t mf35xOilDiagCrc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
    }
  }
  return ~crc;
}

uint32_t mf35xOilDiagRecordCrc(const Mf35xOilDiagRecord& rec) {
  return mf35xOilDiagCrc32(
    reinterpret_cast<const uint8_t*>(&rec),
    offsetof(Mf35xOilDiagRecord, crc32)
  );
}

bool mf35xOilDiagRecordValid(const Mf35xOilDiagRecord& rec) {
  return rec.magic == MF35X_OIL_DIAG_MAGIC &&
         rec.version == MF35X_OIL_DIAG_VERSION &&
         rec.size == sizeof(Mf35xOilDiagRecord) &&
         rec.status <= MF35X_OIL_DIAG_ADS_OFFLINE &&
         rec.crc32 == mf35xOilDiagRecordCrc(rec);
}

const char* mf35xOilDiagStatusText(uint8_t status) {
  switch (status) {
    case MF35X_OIL_DIAG_OK: return "OK";
    case MF35X_OIL_DIAG_CLAMP_ZERO: return "CLAMP_ZERO";
    case MF35X_OIL_DIAG_SHORT_OR_LOW: return "SHORT_OR_LOW";
    case MF35X_OIL_DIAG_OPEN_OR_HIGH: return "OPEN_OR_HIGH";
    case MF35X_OIL_DIAG_INVALID: return "INVALID";
    case MF35X_OIL_DIAG_ADS_OFFLINE: return "ADS_OFFLINE";
    default: return "NO_DATA";
  }
}

uint8_t mf35xOilDiagPriority(uint8_t status) {
  switch (status) {
    case MF35X_OIL_DIAG_ADS_OFFLINE: return 6;
    case MF35X_OIL_DIAG_INVALID: return 5;
    case MF35X_OIL_DIAG_OPEN_OR_HIGH: return 4;
    case MF35X_OIL_DIAG_SHORT_OR_LOW: return 4;
    case MF35X_OIL_DIAG_CLAMP_ZERO: return 2;
    case MF35X_OIL_DIAG_OK: return 1;
    default: return 0;
  }
}

int16_t mf35xOilDiagScaleSigned(float value, float factor) {
  if (!isfinite(value)) return 0;
  long v = lroundf(value * factor);
  if (v < INT16_MIN) v = INT16_MIN;
  if (v > INT16_MAX) v = INT16_MAX;
  return (int16_t)v;
}

uint16_t mf35xOilDiagScaleUnsigned(float value, float factor) {
  if (!isfinite(value) || value < 0.0f) return 0;
  long v = lroundf(value * factor);
  if (v < 0) v = 0;
  if (v > 65535L) v = 65535L;
  return (uint16_t)v;
}

void mf35xOilDiagObserve(
  bool adsReady,
  bool rawAvailable,
  int16_t adcAvg,
  int16_t adcMin,
  int16_t adcMax,
  float voltage,
  float ohm,
  float rawBar,
  float finalBar,
  uint8_t status
) {
  (void)adsReady;
  (void)voltage;
  (void)finalBar;
  Mf35xOilDiagWindow& w = mf35xOilDiagWindow;
  w.sampleCount++;

  if (rawAvailable) {
    w.rawCount++;
    w.adcSum += adcAvg;
    if (adcMin < w.adcMin) w.adcMin = adcMin;
    if (adcMax > w.adcMax) w.adcMax = adcMax;
  }
  if (isfinite(ohm)) {
    w.ohmSum += ohm;
    w.ohmCount++;
  }
  if (isfinite(rawBar)) {
    w.rawBarSum += rawBar;
    w.rawBarCount++;
  }
  if (status == MF35X_OIL_DIAG_SHORT_OR_LOW ||
      status == MF35X_OIL_DIAG_OPEN_OR_HIGH ||
      status == MF35X_OIL_DIAG_INVALID ||
      status == MF35X_OIL_DIAG_ADS_OFFLINE) {
    w.invalidCount++;
  }
  if (mf35xOilDiagPriority(status) > mf35xOilDiagPriority(w.status)) {
    w.status = status;
  }
}

void mf35xOilDiagWindowReset() {
  mf35xOilDiagWindow = Mf35xOilDiagWindow{};
}

void mf35xOilDiagCapture(uint32_t bootId, uint32_t sequence) {
  const Mf35xOilDiagWindow& w = mf35xOilDiagWindow;
  Mf35xOilDiagRecord rec = {};
  rec.magic = MF35X_OIL_DIAG_MAGIC;
  rec.version = MF35X_OIL_DIAG_VERSION;
  rec.size = sizeof(Mf35xOilDiagRecord);
  rec.bootId = bootId;
  rec.sequence = sequence;
  rec.sampleCount = (uint16_t)(w.sampleCount > 65535U ? 65535U : w.sampleCount);
  rec.rawCount = (uint16_t)(w.rawCount > 65535U ? 65535U : w.rawCount);
  rec.invalidCount = (uint16_t)(w.invalidCount > 65535U ? 65535U : w.invalidCount);
  rec.status = w.sampleCount > 0 ? w.status : MF35X_OIL_DIAG_NO_DATA;

  if (w.rawCount > 0) {
    long avg = lround((double)w.adcSum / (double)w.rawCount);
    if (avg < INT16_MIN) avg = INT16_MIN;
    if (avg > INT16_MAX) avg = INT16_MAX;
    rec.adcAvg = (int16_t)avg;
    rec.adcMin = w.adcMin;
    rec.adcMax = w.adcMax;
  }
  if (w.ohmCount > 0) {
    rec.ohmDeci = mf35xOilDiagScaleUnsigned((float)(w.ohmSum / w.ohmCount), 10.0f);
  }
  if (w.rawBarCount > 0) {
    rec.rawPressureCenti = mf35xOilDiagScaleSigned((float)(w.rawBarSum / w.rawBarCount), 100.0f);
  }
  rec.state = MF35X_OIL_DIAG_PENDING;
  rec.crc32 = mf35xOilDiagRecordCrc(rec);
  mf35xOilDiagStaged = rec;
  mf35xOilDiagStagedValid = true;
  mf35xOilDiagWindowReset();
}

void mf35xOilDiagRepairQueue() {
  if (!LittleFS.exists(MF35X_OIL_DIAG_QUEUE)) return;
  File src = LittleFS.open(MF35X_OIL_DIAG_QUEUE, "r");
  if (!src) return;
  const size_t original = src.size();
  src.close();
  const size_t validSize = original - (original % sizeof(Mf35xOilDiagRecord));
  if (validSize == original) return;

  LittleFS.remove(MF35X_OIL_DIAG_REPAIR);
  src = LittleFS.open(MF35X_OIL_DIAG_QUEUE, "r");
  File dst = LittleFS.open(MF35X_OIL_DIAG_REPAIR, "w");
  if (!src || !dst) {
    if (src) src.close();
    if (dst) dst.close();
    LittleFS.remove(MF35X_OIL_DIAG_REPAIR);
    mf35xOilDiagLastError = "diagnostic queue repair open failed";
    return;
  }

  uint8_t buf[168];
  size_t remaining = validSize;
  bool ok = true;
  while (remaining > 0) {
    const size_t n = remaining > sizeof(buf) ? sizeof(buf) : remaining;
    if (src.read(buf, n) != n || dst.write(buf, n) != n) {
      ok = false;
      break;
    }
    remaining -= n;
  }
  dst.flush();
  src.close();
  dst.close();

  if (!ok || remaining != 0 || !LittleFS.remove(MF35X_OIL_DIAG_QUEUE) ||
      !LittleFS.rename(MF35X_OIL_DIAG_REPAIR, MF35X_OIL_DIAG_QUEUE)) {
    LittleFS.remove(MF35X_OIL_DIAG_REPAIR);
    mf35xOilDiagLastError = "diagnostic queue repair failed";
    return;
  }
  mf35xOilDiagCorrupt++;
}

bool mf35xOilDiagFind(uint32_t bootId, uint32_t sequence, Mf35xOilDiagRecord& out, size_t* offsetOut = nullptr) {
  mf35xOilDiagRepairQueue();
  if (!LittleFS.exists(MF35X_OIL_DIAG_QUEUE)) return false;
  File f = LittleFS.open(MF35X_OIL_DIAG_QUEUE, "r");
  if (!f) return false;
  size_t offset = 0;
  Mf35xOilDiagRecord rec;
  while (f.available() >= (int)sizeof(rec)) {
    if (f.read(reinterpret_cast<uint8_t*>(&rec), sizeof(rec)) != sizeof(rec)) break;
    if (!mf35xOilDiagRecordValid(rec)) {
      mf35xOilDiagCorrupt++;
      offset += sizeof(rec);
      continue;
    }
    if (rec.bootId == bootId && rec.sequence == sequence && rec.state != MF35X_OIL_DIAG_SENT) {
      out = rec;
      if (offsetOut) *offsetOut = offset;
      f.close();
      return true;
    }
    offset += sizeof(rec);
  }
  f.close();
  return false;
}

bool mf35xOilDiagPersist(uint32_t bootId, uint32_t sequence) {
  if (!mf35xOilDiagStagedValid ||
      mf35xOilDiagStaged.bootId != bootId ||
      mf35xOilDiagStaged.sequence != sequence) {
    return false;
  }

  const size_t total = LittleFS.totalBytes();
  const size_t used = LittleFS.usedBytes();
  const size_t freeBytes = total > used ? total - used : 0;
  if (freeBytes <= MF35X_OIL_DIAG_FLASH_RESERVE + sizeof(Mf35xOilDiagRecord)) {
    mf35xOilDiagDropped++;
    mf35xOilDiagLastError = "not enough flash for oil diagnostic companion";
    return false;
  }

  File f = LittleFS.open(MF35X_OIL_DIAG_QUEUE, FILE_APPEND);
  if (!f) {
    mf35xOilDiagDropped++;
    mf35xOilDiagLastError = "oil diagnostic queue open failed";
    return false;
  }
  const size_t written = f.write(
    reinterpret_cast<const uint8_t*>(&mf35xOilDiagStaged),
    sizeof(mf35xOilDiagStaged)
  );
  f.flush();
  f.close();
  if (written != sizeof(mf35xOilDiagStaged)) {
    mf35xOilDiagDropped++;
    mf35xOilDiagLastError = "oil diagnostic queue write incomplete";
    return false;
  }
  mf35xOilDiagQueued++;
  return true;
}

void mf35xOilDiagJsonComma(String& json, bool& first) {
  if (!first) json += ',';
  first = false;
}

void mf35xOilDiagJsonRaw(String& json, bool& first, const char* key, const String& raw) {
  mf35xOilDiagJsonComma(json, first);
  json += '"'; json += key; json += "\":"; json += raw;
}

void mf35xOilDiagJsonText(String& json, bool& first, const char* key, const char* value) {
  mf35xOilDiagJsonComma(json, first);
  json += '"'; json += key; json += "\":\""; json += value; json += '"';
}

void mf35xOilDiagAppendNulls(String& json, bool& first) {
  for (const char* key : {
    "oil_pressure_adc_avg", "oil_pressure_adc_min", "oil_pressure_adc_max",
    "oil_pressure_voltage_avg", "oil_pressure_voltage_min", "oil_pressure_voltage_max",
    "oil_pressure_ohm", "oil_pressure_raw_bar"
  }) {
    mf35xOilDiagJsonRaw(json, first, key, "null");
  }
  mf35xOilDiagJsonRaw(json, first, "oil_pressure_diag_samples", "0");
  mf35xOilDiagJsonRaw(json, first, "oil_pressure_diag_invalid", "0");
  mf35xOilDiagJsonText(json, first, "oil_pressure_diag_status", "NO_DIAG");
}

void mf35xOilDiagAppendJson(
  String& json,
  bool& first,
  uint32_t bootId,
  uint32_t sequence,
  bool replay
) {
  (void)replay;
  Mf35xOilDiagRecord rec = {};
  bool have = false;
  if (mf35xOilDiagStagedValid &&
      mf35xOilDiagStaged.bootId == bootId &&
      mf35xOilDiagStaged.sequence == sequence) {
    rec = mf35xOilDiagStaged;
    have = true;
  } else {
    have = mf35xOilDiagFind(bootId, sequence, rec, nullptr);
  }

  if (!have) {
    mf35xOilDiagAppendNulls(json, first);
    return;
  }

  if (rec.rawCount > 0) {
    mf35xOilDiagJsonRaw(json, first, "oil_pressure_adc_avg", String(rec.adcAvg));
    mf35xOilDiagJsonRaw(json, first, "oil_pressure_adc_min", String(rec.adcMin));
    mf35xOilDiagJsonRaw(json, first, "oil_pressure_adc_max", String(rec.adcMax));
    // ADS1115 GAIN_ONE = 0.125 mV/bit.
    mf35xOilDiagJsonRaw(json, first, "oil_pressure_voltage_avg", String((double)rec.adcAvg * 0.000125, 6));
    mf35xOilDiagJsonRaw(json, first, "oil_pressure_voltage_min", String((double)rec.adcMin * 0.000125, 6));
    mf35xOilDiagJsonRaw(json, first, "oil_pressure_voltage_max", String((double)rec.adcMax * 0.000125, 6));
  } else {
    for (const char* key : {
      "oil_pressure_adc_avg", "oil_pressure_adc_min", "oil_pressure_adc_max",
      "oil_pressure_voltage_avg", "oil_pressure_voltage_min", "oil_pressure_voltage_max"
    }) mf35xOilDiagJsonRaw(json, first, key, "null");
  }

  if (rec.ohmDeci > 0) {
    mf35xOilDiagJsonRaw(json, first, "oil_pressure_ohm", String((double)rec.ohmDeci / 10.0, 1));
  } else {
    mf35xOilDiagJsonRaw(json, first, "oil_pressure_ohm", "null");
  }

  if (rec.rawCount > 0 && rec.status != MF35X_OIL_DIAG_SHORT_OR_LOW &&
      rec.status != MF35X_OIL_DIAG_OPEN_OR_HIGH && rec.status != MF35X_OIL_DIAG_ADS_OFFLINE) {
    mf35xOilDiagJsonRaw(json, first, "oil_pressure_raw_bar", String((double)rec.rawPressureCenti / 100.0, 2));
  } else {
    mf35xOilDiagJsonRaw(json, first, "oil_pressure_raw_bar", "null");
  }
  mf35xOilDiagJsonRaw(json, first, "oil_pressure_diag_samples", String(rec.sampleCount));
  mf35xOilDiagJsonRaw(json, first, "oil_pressure_diag_invalid", String(rec.invalidCount));
  mf35xOilDiagJsonText(json, first, "oil_pressure_diag_status", mf35xOilDiagStatusText(rec.status));
}

void mf35xOilDiagMarkDelivered(uint32_t bootId, uint32_t sequence, bool noBasePending) {
  if (mf35xOilDiagStagedValid &&
      mf35xOilDiagStaged.bootId == bootId &&
      mf35xOilDiagStaged.sequence == sequence) {
    mf35xOilDiagStagedValid = false;
  }

  Mf35xOilDiagRecord rec;
  size_t offset = 0;
  if (mf35xOilDiagFind(bootId, sequence, rec, &offset)) {
    File f = LittleFS.open(MF35X_OIL_DIAG_QUEUE, "r+");
    if (f && f.seek(offset + offsetof(Mf35xOilDiagRecord, state), SeekSet)) {
      const uint8_t sent = MF35X_OIL_DIAG_SENT;
      f.write(&sent, 1);
      f.flush();
    }
    if (f) f.close();
  }

  // If no base race sample is pending, any remaining diagnostic-only pending
  // record is necessarily orphaned after a completed deterministic PUT.
  if (noBasePending) {
    LittleFS.remove(MF35X_OIL_DIAG_QUEUE);
    LittleFS.remove(MF35X_OIL_DIAG_REPAIR);
  }
}
'''
write("esp32/MF35X_Livetracker/oil_pressure_diagnostics.hpp", diag_header)

# Include diagnostics before the core so offline_race_buffer.hpp can call it.
ino = "esp32/MF35X_Livetracker/MF35X_Livetracker.ino"
replace_exact(
    ino,
    '#include <freertos/semphr.h>\n\nvoid mf35xAttachStableRpmInterrupt(int pin, int mode);',
    '#include <freertos/semphr.h>\n#include "oil_pressure_diagnostics.hpp"\n\nvoid mf35xAttachStableRpmInterrupt(int pin, int mode);',
)
replace_exact(
    ino,
    "MF35X Livetracker V5.9.16 OTA SIGNED",
    "MF35X Livetracker V5.9.17 OTA SIGNED",
)
replace_exact(
    ino,
    "  - schnelle GPIO11-Steuerung bleibt netzunabhaengig",
    "  - schnelle GPIO11-Steuerung bleibt netzunabhaengig\n  - stromausfallsichere Maxwert-Generationen + Oeldruck-Renndiagnose",
)

offline = "esp32/MF35X_Livetracker/offline_race_buffer.hpp"
replace_exact(
    offline,
    '''  rec.capturedMillis = millis();
  rec.state = OFFLINE_STATE_PENDING;

  const GpsSnapshot gpsDaten = gpsSnapshotLesen();''',
    '''  rec.capturedMillis = millis();
  rec.state = OFFLINE_STATE_PENDING;

  // Snapshot the diagnostic window using the same deterministic sample key.
  mf35xOilDiagCapture(rec.bootId, rec.sequence);

  const GpsSnapshot gpsDaten = gpsSnapshotLesen();'''
)
replace_exact(
    offline,
    '''  if ((rec.flags & OFFLINE_FLAG_OIL_PRESSURE_VALID) != 0) {
    jsonFloatFeld(json, first, "oil_pressure", (double)rec.oilPressureCenti / 100.0, 2);
  } else {
    jsonRaw(json, first, "oil_pressure", "null");
  }

  if ((rec.flags & OFFLINE_FLAG_OIL_TEMP_VALID) != 0) {''',
    '''  if ((rec.flags & OFFLINE_FLAG_OIL_PRESSURE_VALID) != 0) {
    jsonFloatFeld(json, first, "oil_pressure", (double)rec.oilPressureCenti / 100.0, 2);
  } else {
    jsonRaw(json, first, "oil_pressure", "null");
  }

  // Companion data never changes the legacy 64-byte OfflineRaceRecord layout.
  mf35xOilDiagAppendJson(json, first, rec.bootId, rec.sequence, replay);

  if ((rec.flags & OFFLINE_FLAG_OIL_TEMP_VALID) != 0) {'''
)
replace_exact(
    offline,
    '''  offlinePendingCount++;
  offlineQueuedCount++;
  offlinePsramCacheSpeichern(rec);
  offlineFsStatusAktualisieren();
  return true;''',
    '''  offlinePendingCount++;
  offlineQueuedCount++;
  offlinePsramCacheSpeichern(rec);
  // Main race data has priority. Diagnostic companion failure must never make
  // a valid 64-byte race record fail or disappear.
  mf35xOilDiagPersist(rec.bootId, rec.sequence);
  offlineFsStatusAktualisieren();
  return true;'''
)
replace_exact(
    offline,
    '''    if (offlineRecordSenden(recordingConfig.raceId, rec, false)) {
      historyOk++;
      return;
    }''',
    '''    if (offlineRecordSenden(recordingConfig.raceId, rec, false)) {
      historyOk++;
      mf35xOilDiagMarkDelivered(rec.bootId, rec.sequence, true);
      return;
    }'''
)
replace_exact(
    offline,
    '''  if (offlinePendingCount > 0) offlinePendingCount--;
  offlineReplayedCount++;
  historyOk++;
  offlineLastError = "";''',
    '''  if (offlinePendingCount > 0) offlinePendingCount--;
  mf35xOilDiagMarkDelivered(rec.bootId, rec.sequence, offlinePendingCount == 0);
  offlineReplayedCount++;
  historyOk++;
  offlineLastError = "";'''
)

print("Prepared website + firmware hardening for V5.9.17")
