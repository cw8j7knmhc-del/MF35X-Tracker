#pragma once

// ==================================================
// MF35X V5.9.18 - RPM-/GPIO11-RENNDIAGNOSE
// ==================================================
// Der bestehende 64-Byte-Rennrecord V1 bleibt unveraendert und damit
// rueckwaertskompatibel. Diese Zusatzdaten werden nach jedem Basissample per
// PATCH an denselben Firebase-Sample angehaengt. Wenn der Basissample offline
// im LittleFS liegt, wird auch die RPM-Diagnose in einer separaten Companion-
// Queue gepuffert und erst nach erfolgreichem Basissample nachgesendet.
//
// Gespeichert werden:
// - ungefilterte, aus ALLEN GPIO10-Flanken berechnete RPM
// - schnelle plausibilisierte RPM, die GPIO11 wirklich verwendet
// - geglaettete Anzeige-/Logging-RPM
// - Roh-/akzeptiert-/verworfen-/Doppelflanken-Zaehler
// - verworfene/Doppelflanken seit dem vorherigen Rennsample
// - Referenzperiode, Filter-Lock, Signalstatus und GPIO11
// ==================================================

constexpr uint32_t MF35X_RPM_DIAG_MAGIC = 0x4D463138UL; // "MF18"
constexpr uint16_t MF35X_RPM_DIAG_VERSION = 1;
constexpr uint8_t MF35X_RPM_DIAG_PENDING = 0xA5;
constexpr uint8_t MF35X_RPM_DIAG_SENT = 0x5A;
constexpr unsigned long MF35X_RPM_DIAG_DRAIN_MS = 300UL;
constexpr size_t MF35X_RPM_DIAG_FLASH_PROTECT_BYTES = 640UL * 1024UL;

constexpr uint8_t MF35X_RPM_DIAG_FLAG_SIGNAL_OK = 1u << 0;
constexpr uint8_t MF35X_RPM_DIAG_FLAG_FILTER_LOCKED = 1u << 1;
constexpr uint8_t MF35X_RPM_DIAG_FLAG_GPIO11 = 1u << 2;
constexpr uint8_t MF35X_RPM_DIAG_FLAG_RAW_VALID = 1u << 3;
constexpr uint8_t MF35X_RPM_DIAG_FLAG_FILTERED_VALID = 1u << 4;
constexpr uint8_t MF35X_RPM_DIAG_FLAG_DISPLAY_VALID = 1u << 5;

#pragma pack(push, 1)
struct Mf35xRpmDiagRecord {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t bootId;
  uint32_t sequence;
  uint16_t rawRpmDeci;
  uint16_t filteredRpmDeci;
  uint16_t displayRpmDeci;
  uint32_t rawEdgesTotal;
  uint32_t acceptedEdgesTotal;
  uint32_t rejectedEdgesTotal;
  uint32_t doubleEdgesTotal;
  uint32_t reacquireTotal;
  uint32_t referencePeriodUs;
  uint16_t rejectedSinceLastSample;
  uint16_t doubleSinceLastSample;
  uint8_t periodCount;
  uint8_t flags;
  uint32_t crc32;
  uint8_t state;
  uint8_t padding[3];
};
#pragma pack(pop)

static_assert(sizeof(Mf35xRpmDiagRecord) == 60, "Mf35xRpmDiagRecord muss 60 Byte gross sein");

unsigned long mf35xRpmDiagLastDrainMs = 0;
uint32_t mf35xRpmDiagLastRejected = 0;
uint32_t mf35xRpmDiagLastDouble = 0;
uint32_t mf35xRpmDiagQueued = 0;
uint32_t mf35xRpmDiagReplayed = 0;
uint32_t mf35xRpmDiagDropped = 0;

uint32_t mf35xRpmDiagRecordCrc(const Mf35xRpmDiagRecord& rec) {
  return offlineCrc32(
    reinterpret_cast<const uint8_t*>(&rec),
    offsetof(Mf35xRpmDiagRecord, crc32)
  );
}

bool mf35xRpmDiagRecordValid(const Mf35xRpmDiagRecord& rec) {
  return rec.magic == MF35X_RPM_DIAG_MAGIC &&
         rec.version == MF35X_RPM_DIAG_VERSION &&
         rec.size == sizeof(Mf35xRpmDiagRecord) &&
         rec.crc32 == mf35xRpmDiagRecordCrc(rec);
}

uint16_t mf35xRpmDiagDeci(float value) {
  if (!isfinite(value) || value < 0.0f) return 0;
  long v = lroundf(value * 10.0f);
  if (v < 0) v = 0;
  if (v > 65535L) v = 65535L;
  return (uint16_t)v;
}

uint16_t mf35xRpmDiagDelta16(uint32_t current, uint32_t previous) {
  const uint32_t delta = current - previous;
  return delta > 65535UL ? 65535U : (uint16_t)delta;
}

String mf35xRpmDiagQueuePath(const String& raceId) {
  return String("/rd_") + raceId + ".bin";
}

String mf35xRpmDiagRaceIdFromPath(String path) {
  if (!path.startsWith("/")) path = "/" + path;
  if (!path.startsWith("/rd_") || !path.endsWith(".bin")) return "";
  return path.substring(4, path.length() - 4);
}

Mf35xRpmDiagRecord mf35xRpmDiagBuild(uint32_t sequence) {
  Mf35xRpmDiagRecord rec = {};
  rec.magic = MF35X_RPM_DIAG_MAGIC;
  rec.version = MF35X_RPM_DIAG_VERSION;
  rec.size = sizeof(Mf35xRpmDiagRecord);
  rec.bootId = offlineBootId;
  rec.sequence = sequence;
  rec.state = MF35X_RPM_DIAG_PENDING;

  float rawRpm = 0.0f;
  float filteredRpm = 0.0f;
  float displayRpm = 0.0f;
  uint32_t rawEdges = 0;
  uint32_t acceptedEdges = 0;
  uint32_t rejectedEdges = 0;
  uint32_t doubleEdges = 0;
  uint32_t reacquires = 0;
  uint32_t referenceUs = 0;
  uint8_t periodCount = 0;

  portENTER_CRITICAL(&mf35xRpmMux);
  rawRpm = mf35xRpmRohUngefiltert;
  filteredRpm = mf35xRpmSchnell;
  displayRpm = rpm;
  rawEdges = mf35xRpmRohImpulseGesamt;
  acceptedEdges = mf35xRpmAkzeptierteImpulseGesamt;
  rejectedEdges = mf35xRpmVerworfeneImpulse;
  doubleEdges = mf35xRpmDoppelImpulse;
  reacquires = mf35xRpmNeuerfassungen;
  referenceUs = mf35xRpmReferenzPeriodeUs;
  periodCount = mf35xRpmPeriodenCount;
  portEXIT_CRITICAL(&mf35xRpmMux);

  rec.rawRpmDeci = mf35xRpmDiagDeci(rawRpm);
  rec.filteredRpmDeci = mf35xRpmDiagDeci(filteredRpm);
  rec.displayRpmDeci = mf35xRpmDiagDeci(displayRpm);
  rec.rawEdgesTotal = rawEdges;
  rec.acceptedEdgesTotal = acceptedEdges;
  rec.rejectedEdgesTotal = rejectedEdges;
  rec.doubleEdgesTotal = doubleEdges;
  rec.reacquireTotal = reacquires;
  rec.referencePeriodUs = referenceUs;
  rec.rejectedSinceLastSample = mf35xRpmDiagDelta16(rejectedEdges, mf35xRpmDiagLastRejected);
  rec.doubleSinceLastSample = mf35xRpmDiagDelta16(doubleEdges, mf35xRpmDiagLastDouble);
  rec.periodCount = periodCount;

  mf35xRpmDiagLastRejected = rejectedEdges;
  mf35xRpmDiagLastDouble = doubleEdges;

  if (rpmSignalOk) rec.flags |= MF35X_RPM_DIAG_FLAG_SIGNAL_OK;
  if (referenceUs != 0 && periodCount >= MF35X_RPM_MIN_PERIODEN) {
    rec.flags |= MF35X_RPM_DIAG_FLAG_FILTER_LOCKED;
  }
  if (schaltausgangAktiv) rec.flags |= MF35X_RPM_DIAG_FLAG_GPIO11;
  if (isfinite(rawRpm)) rec.flags |= MF35X_RPM_DIAG_FLAG_RAW_VALID;
  if (rpmSignalOk && isfinite(filteredRpm)) rec.flags |= MF35X_RPM_DIAG_FLAG_FILTERED_VALID;
  if (rpmSignalOk && isfinite(displayRpm)) rec.flags |= MF35X_RPM_DIAG_FLAG_DISPLAY_VALID;

  rec.crc32 = mf35xRpmDiagRecordCrc(rec);
  return rec;
}

String mf35xRpmDiagJson(const Mf35xRpmDiagRecord& rec) {
  String json = "{";
  json.reserve(760);
  bool first = true;

  if ((rec.flags & MF35X_RPM_DIAG_FLAG_RAW_VALID) != 0) {
    jsonFloatFeld(json, first, "rpm_raw_unfiltered", (double)rec.rawRpmDeci / 10.0, 1);
  } else {
    jsonRaw(json, first, "rpm_raw_unfiltered", "null");
  }

  if ((rec.flags & MF35X_RPM_DIAG_FLAG_FILTERED_VALID) != 0) {
    jsonFloatFeld(json, first, "rpm_filtered", (double)rec.filteredRpmDeci / 10.0, 1);
  } else {
    jsonRaw(json, first, "rpm_filtered", "null");
  }

  if ((rec.flags & MF35X_RPM_DIAG_FLAG_DISPLAY_VALID) != 0) {
    jsonFloatFeld(json, first, "rpm_display", (double)rec.displayRpmDeci / 10.0, 1);
  } else {
    jsonRaw(json, first, "rpm_display", "null");
  }

  jsonULongFeld(json, first, "rpm_raw_edges_total", rec.rawEdgesTotal);
  jsonULongFeld(json, first, "rpm_accepted_edges_total", rec.acceptedEdgesTotal);
  jsonULongFeld(json, first, "rpm_rejected_edges_total", rec.rejectedEdgesTotal);
  jsonULongFeld(json, first, "rpm_double_edges_total", rec.doubleEdgesTotal);
  jsonULongFeld(json, first, "rpm_reacquire_total", rec.reacquireTotal);
  jsonULongFeld(json, first, "rpm_reference_period_us", rec.referencePeriodUs);
  jsonULongFeld(json, first, "rpm_rejected_since_last_sample", rec.rejectedSinceLastSample);
  jsonULongFeld(json, first, "rpm_double_since_last_sample", rec.doubleSinceLastSample);
  jsonULongFeld(json, first, "rpm_period_count", rec.periodCount);
  jsonBoolFeld(json, first, "rpm_filter_locked",
               (rec.flags & MF35X_RPM_DIAG_FLAG_FILTER_LOCKED) != 0);
  jsonBoolFeld(json, first, "rpm_signal_ok",
               (rec.flags & MF35X_RPM_DIAG_FLAG_SIGNAL_OK) != 0);
  jsonBoolFeld(json, first, "gpio11",
               (rec.flags & MF35X_RPM_DIAG_FLAG_GPIO11) != 0);
  json += '}';
  return json;
}

bool mf35xRpmDiagPatchSample(
  const String& raceId,
  const Mf35xRpmDiagRecord& rec,
  bool requireBaseSample
) {
  if (WiFi.status() != WL_CONNECTED || !offlineRaceIdGueltig(raceId)) return false;

  String path = "tracker/races/" + raceId + "/samples/" +
                mf35xDiagSampleId(rec.bootId, rec.sequence);

  if (requireBaseSample) {
    String existing;
    if (!firebaseGet(path, existing)) return false;
    if (existing == "null" || existing.indexOf("\"timestamp\"") < 0) return false;
  }

  return firebasePatch(path, mf35xRpmDiagJson(rec), false);
}

bool mf35xRpmDiagQueueAppend(const String& raceId, const Mf35xRpmDiagRecord& rec) {
  if (!offlineBufferReady || !offlineRaceIdGueltig(raceId)) return false;

  offlineFsStatusAktualisieren();
  const size_t freeBytes = offlineFsTotalBytes > offlineFsUsedBytes
    ? offlineFsTotalBytes - offlineFsUsedBytes
    : 0;

  // Der 64-Byte-Basis-Rennpuffer hat immer Vorrang. RPM-Diagnose wird frueher
  // verworfen als ein normaler Rennsample, falls der Flash knapp wird.
  if (freeBytes <= MF35X_RPM_DIAG_FLASH_PROTECT_BYTES + sizeof(rec)) {
    mf35xRpmDiagDropped++;
    return false;
  }

  File f = LittleFS.open(mf35xRpmDiagQueuePath(raceId), FILE_APPEND);
  if (!f) {
    mf35xRpmDiagDropped++;
    return false;
  }

  const size_t written =
    f.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(rec));
  f.flush();
  f.close();

  if (written != sizeof(rec)) {
    mf35xRpmDiagDropped++;
    return false;
  }

  mf35xRpmDiagQueued++;
  offlineFsStatusAktualisieren();
  return true;
}

bool mf35xRpmDiagMarkSent(File& f, size_t offset) {
  if (!f.seek(offset + offsetof(Mf35xRpmDiagRecord, state), SeekSet)) return false;
  const uint8_t sent = MF35X_RPM_DIAG_SENT;
  if (f.write(&sent, 1) != 1) return false;
  f.flush();
  return true;
}

bool mf35xRpmDiagFileHasPending(const String& path) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;

  Mf35xRpmDiagRecord rec;
  while (f.available() >= (int)sizeof(rec)) {
    if (f.read(reinterpret_cast<uint8_t*>(&rec), sizeof(rec)) != sizeof(rec)) break;
    if (mf35xRpmDiagRecordValid(rec) && rec.state != MF35X_RPM_DIAG_SENT) {
      f.close();
      return true;
    }
  }

  f.close();
  return false;
}

void mf35xRpmDiagDrainOne() {
  if (!offlineBufferReady || WiFi.status() != WL_CONNECTED) return;

  const unsigned long now = millis();
  if ((unsigned long)(now - mf35xRpmDiagLastDrainMs) < MF35X_RPM_DIAG_DRAIN_MS) return;
  mf35xRpmDiagLastDrainMs = now;

  File root = LittleFS.open("/");
  if (!root) return;

  String path = "";
  File e = root.openNextFile();
  while (e) {
    String n = e.name();
    e.close();
    if (!n.startsWith("/")) n = "/" + n;
    if (mf35xRpmDiagRaceIdFromPath(n).length() > 0 && mf35xRpmDiagFileHasPending(n)) {
      path = n;
      break;
    }
    e = root.openNextFile();
  }
  root.close();

  if (path.length() == 0) return;

  const String raceId = mf35xRpmDiagRaceIdFromPath(path);
  File f = LittleFS.open(path, "r+");
  if (!f) return;

  size_t offset = 0;
  Mf35xRpmDiagRecord rec;
  bool found = false;

  while (f.available() >= (int)sizeof(rec)) {
    if (f.read(reinterpret_cast<uint8_t*>(&rec), sizeof(rec)) != sizeof(rec)) break;

    if (mf35xRpmDiagRecordValid(rec) && rec.state != MF35X_RPM_DIAG_SENT) {
      found = true;
      if (mf35xRpmDiagPatchSample(raceId, rec, true)) {
        if (mf35xRpmDiagMarkSent(f, offset)) {
          mf35xRpmDiagReplayed++;
        }
      }
      break;
    }

    offset += sizeof(rec);
  }

  f.close();

  if (!found || !mf35xRpmDiagFileHasPending(path)) {
    LittleFS.remove(path);
    offlineFsStatusAktualisieren();
  }
}

void mf35xRpmDiagCapture(uint32_t sequence) {
  if (!recordingConfig.enabled || recordingConfig.raceId.length() == 0 || sequence == 0) return;

  const Mf35xRpmDiagRecord rec = mf35xRpmDiagBuild(sequence);

  // Wenn kein Basis-Rueckstau existiert, hat offlineRennMesspunktBearbeiten()
  // den unmittelbar davor erzeugten Basissample bereits erfolgreich per PUT
  // angelegt. Dann kann ohne zusaetzlichen GET direkt gepatcht werden.
  if (WiFi.status() == WL_CONNECTED && offlinePendingCount == 0) {
    if (mf35xRpmDiagPatchSample(recordingConfig.raceId, rec, false)) return;
  }

  mf35xRpmDiagQueueAppend(recordingConfig.raceId, rec);
}

void mf35xV5918RpmDiagSetup() {
  portENTER_CRITICAL(&mf35xRpmMux);
  mf35xRpmDiagLastRejected = mf35xRpmVerworfeneImpulse;
  mf35xRpmDiagLastDouble = mf35xRpmDoppelImpulse;
  portEXIT_CRITICAL(&mf35xRpmMux);

  Serial.println(
    "V5.9.18 RPM-Diagnose: raw/filtered/rejected/double + GPIO11 + Offline-Companion aktiv"
  );
}

void mf35xV5918RpmDiagLoop(uint32_t raceSequenceBefore) {
  if (recordingConfig.enabled && recordingConfig.raceId.length() > 0 &&
      offlineSampleSequence != raceSequenceBefore) {
    mf35xRpmDiagCapture(offlineSampleSequence);
  }

  mf35xRpmDiagDrainOne();
}
