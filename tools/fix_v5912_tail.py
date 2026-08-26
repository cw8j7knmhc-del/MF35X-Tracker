from pathlib import Path

p = Path('esp32/MF35X_Livetracker/offline_race_buffer.hpp')
s = p.read_text()

start = s.index('void offlineUnvollstaendigeDateiEndenReparieren() {')
end = s.index('\nvoid offlineQueueScannen() {', start)

new_func = r'''void offlineUnvollstaendigeDateiEndenReparieren() {
  if (!offlineBufferReady) return;

  File root = LittleFS.open("/");
  if (!root) return;

  String pfade[8];
  size_t groessen[8];
  size_t anzahl = 0;

  File entry = root.openNextFile();
  while (entry && anzahl < 8) {
    String name = entry.name();
    const size_t size = entry.size();
    entry.close();
    if (!name.startsWith("/")) name = "/" + name;

    if (offlineRaceIdAusPfad(name).length() > 0 &&
        (size % sizeof(OfflineRaceRecord)) != 0) {
      pfade[anzahl] = name;
      groessen[anzahl] = size - (size % sizeof(OfflineRaceRecord));
      anzahl++;
    }
    entry = root.openNextFile();
  }
  root.close();

  uint8_t block[256];

  for (size_t i = 0; i < anzahl; ++i) {
    const String tempPfad = pfade[i] + ".repair";
    LittleFS.remove(tempPfad);

    File quelle = LittleFS.open(pfade[i], "r");
    File ziel = LittleFS.open(tempPfad, "w");
    if (!quelle || !ziel) {
      if (quelle) quelle.close();
      if (ziel) ziel.close();
      LittleFS.remove(tempPfad);
      offlineLastError = "Unvollstaendiges Queue-Dateiende konnte nicht repariert werden";
      continue;
    }

    size_t verbleibend = groessen[i];
    bool ok = true;
    while (verbleibend > 0) {
      const size_t teil = verbleibend > sizeof(block) ? sizeof(block) : verbleibend;
      const size_t gelesen = quelle.read(block, teil);
      if (gelesen != teil || ziel.write(block, teil) != teil) {
        ok = false;
        break;
      }
      verbleibend -= teil;
    }

    ziel.flush();
    quelle.close();
    ziel.close();

    if (!ok || verbleibend != 0) {
      LittleFS.remove(tempPfad);
      offlineLastError = "Unvollstaendiges Queue-Dateiende konnte nicht kopiert werden";
      continue;
    }

    // Erst wenn die Reparaturdatei komplett geschrieben ist, das Original ersetzen.
    if (!LittleFS.remove(pfade[i]) || !LittleFS.rename(tempPfad, pfade[i])) {
      offlineLastError = "Reparierte Queue-Datei konnte nicht aktiviert werden";
      continue;
    }

    offlineCorruptCount++;
  }
}
'''

p.write_text(s[:start] + new_func + s[end:])
