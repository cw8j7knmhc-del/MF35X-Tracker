#pragma once

// ==================================================
// SAFE RACE SYNC - NUR TEST-BRANCH
// ==================================================
// Ziel: RPM-/GPIO11-Diagnose exakt zum Basissample erfassen, ohne die
// bewaehrte RPM-, GPIO11-, GPS-, Sensor- oder Netzwerklogik umzubauen.
//
// offlineRennMesspunktBearbeiten() ruft mf35xRpmDiagPrepare() unmittelbar
// nach offlineRecordBauen() auf. Dabei wird ausschliesslich ein Diagnose-
// Snapshot im RAM erzeugt. Versand/Pufferung erfolgen weiterhin spaeter ueber
// die bereits bewaehrten V5.9.18-Funktionen.
// ==================================================

Mf35xRpmDiagRecord mf35xRpmDiagPreparedRecord = {};
bool mf35xRpmDiagPreparedValid = false;

void mf35xRpmDiagPrepare(uint32_t sequence) {
  if (!recordingConfig.enabled || recordingConfig.raceId.length() == 0 || sequence == 0) {
    mf35xRpmDiagPreparedValid = false;
    return;
  }

  mf35xRpmDiagPreparedRecord = mf35xRpmDiagBuild(sequence);
  mf35xRpmDiagPreparedValid = true;
}

void mf35xRpmDiagCaptureSynced(uint32_t sequence) {
  if (!recordingConfig.enabled || recordingConfig.raceId.length() == 0 || sequence == 0) return;

  Mf35xRpmDiagRecord rec;
  if (mf35xRpmDiagPreparedValid &&
      mf35xRpmDiagPreparedRecord.bootId == offlineBootId &&
      mf35xRpmDiagPreparedRecord.sequence == sequence) {
    rec = mf35xRpmDiagPreparedRecord;
    mf35xRpmDiagPreparedValid = false;
  } else {
    // Sicherheits-Fallback: Falls der Vorbereitungshook aus irgendeinem Grund
    // nicht gelaufen ist, bleibt das bisherige Verhalten erhalten.
    rec = mf35xRpmDiagBuild(sequence);
  }

  // Bestehendes Versand-/Offline-Verhalten unveraendert weiterverwenden.
  if (WiFi.status() == WL_CONNECTED && offlinePendingCount == 0) {
    if (mf35xRpmDiagPatchSample(recordingConfig.raceId, rec, false)) return;
  }

  mf35xRpmDiagQueueAppend(recordingConfig.raceId, rec);
}

void mf35xV5920SafeRpmDiagLoop(uint32_t raceSequenceBefore) {
  if (recordingConfig.enabled && recordingConfig.raceId.length() > 0 &&
      offlineSampleSequence != raceSequenceBefore) {
    mf35xRpmDiagCaptureSynced(offlineSampleSequence);
  }

  // Originale Companion-Queue unveraendert weiter abarbeiten.
  mf35xRpmDiagDrainOne();
}
