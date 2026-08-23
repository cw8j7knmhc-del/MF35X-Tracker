MF35X Live Tracker V9.5.6
=========================

Ziel:
- Besucheransicht optisch exakt in der Struktur von V9.5.1 belassen.
- GPS-/Offline-Fix aus V9.5.5 weiterverwenden.

Auf GitHub NUR diese beiden Dateien vollständig ersetzen:
1. index.html
2. script.js

NICHT ersetzen / unverändert lassen:
- style.css
- admin.html
- admin.js
- analysis.html
- analysis.js
- firebase-config.js
- manifest.json
- tractor.png
- alle sonstigen Dateien

Verhalten:
- ESP32 mit frischem Firebase-Zeitstempel = Online, auch wenn GPS keinen Fix hat.
- Bei GPS ohne Fix zeigt GPS-Qualität "Kein Fix".
- Andere Live-Werte laufen weiter.
- Letzte gültige GPS-Position bleibt auf der Karte.
- Bei echtem ESP32-Offline bleiben letzte Werte/Position sichtbar, Status wird Offline.

Nach GitHub-Upload im Browser Strg+F5 drücken.
