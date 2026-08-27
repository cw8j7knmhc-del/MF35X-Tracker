#pragma once

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
