MF35X TRACKER – OFFLINE-FLACKERN FIX V9.5.3
============================================

Zu ersetzen auf GitHub:
  script.js

Vorgehen:
1. Im GitHub-Repository MF35X-Tracker die vorhandene Datei script.js öffnen.
2. Die vorhandene script.js vollständig durch die beiliegende script.js ersetzen.
3. Änderung committen.
4. Website neu laden; auf iPhone/Safari bei Bedarf Seite einmal komplett neu laden.

Geändert:
- Offline-Timeout mindestens 5 Sekunden.
- Bei größerem uploadIntervalMs: Timeout automatisch 3 x Uploadintervall.
- Maximaler Timeout weiterhin 15 Sekunden.
- Kurze Firebase-/Browser-Verzögerungen verursachen kein Online/Offline-Flackern mehr.
- Bei echtem Offline-Zustand bleiben die letzten gültigen Messwerte sichtbar.
- Der Status zeigt weiterhin klar "Offline".
- Im Feld Verbindung steht bei echtem Timeout "Keine neuen Daten".
- Alarmfarben und Alarmbanner werden bei Offline-Daten deaktiviert, damit alte Werte
  nicht fälschlich als aktuelle Alarme wirken.

Nicht geändert:
- ESP32-Code
- Firebase-Struktur
- index.html
- style.css
- admin.html / admin.js
- analysis.html / analysis.js
- Sensor-Grenzwerte
- Karten-/GPS-Funktion
