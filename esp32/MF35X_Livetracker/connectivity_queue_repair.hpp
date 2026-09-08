#pragma once

// Repariert nach Stromausfall ein eventuell nur teilweise geschriebenes Ende
// einer Netzwerk-Companion-Datei. Ohne diese Reparatur wuerde ein spaeter
// angehaengter Record hinter einem Teilrecord nicht mehr am festen Raster liegen.
void mf35xNetDiagRepairQueueFiles() {
  if (!offlineBufferReady) return;

  File root = LittleFS.open("/");
  if (!root) return;

  String paths[8];
  size_t validBytes[8] = {};
  size_t count = 0;

  File entry = root.openNextFile();
  while (entry && count < 8) {
    String name = entry.name();
    const size_t size = entry.size();
    entry.close();
    if (!name.startsWith("/")) name = "/" + name;

    if (mf35xNetDiagRaceIdFromPath(name).length() > 0 &&
        (size % sizeof(Mf35xNetDiagRecord)) != 0) {
      paths[count] = name;
      validBytes[count] = size - (size % sizeof(Mf35xNetDiagRecord));
      count++;
    }
    entry = root.openNextFile();
  }
  root.close();

  uint8_t buffer[256];
  for (size_t i = 0; i < count; ++i) {
    const String tmp = paths[i] + ".repair";
    LittleFS.remove(tmp);

    File source = LittleFS.open(paths[i], "r");
    File target = LittleFS.open(tmp, "w");
    if (!source || !target) {
      if (source) source.close();
      if (target) target.close();
      LittleFS.remove(tmp);
      continue;
    }

    size_t remaining = validBytes[i];
    bool ok = true;
    while (remaining > 0) {
      const size_t part = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
      if (source.read(buffer, part) != part || target.write(buffer, part) != part) {
        ok = false;
        break;
      }
      remaining -= part;
    }

    target.flush();
    source.close();
    target.close();

    if (!ok || remaining != 0) {
      LittleFS.remove(tmp);
      continue;
    }

    // Erst die komplett geschriebene Reparaturdatei aktivieren.
    if (LittleFS.remove(paths[i])) {
      LittleFS.rename(tmp, paths[i]);
    } else {
      LittleFS.remove(tmp);
    }
  }

  offlineFsStatusAktualisieren();
}
