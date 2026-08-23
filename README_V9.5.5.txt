MF35X V5.9.2 / Website V9.5.5 – GPS-/Offline-Fix
=====================================================

ESP32:
- MF35X_Livetracker_V5_9_2.ino komplett in Arduino IDE verwenden.
- Bei fehlendem GPS-Fix werden lat/lng nicht mehr mit null geloescht.
- gps_valid=false kennzeichnet weiterhin eindeutig den fehlenden GPS-Fix.
- Geschwindigkeit wird ohne Fix weiterhin auf 0.0 km/h gesetzt.
- Alle anderen bisherigen Funktionen von V5.9.1 bleiben erhalten.

GitHub / Website:
Diese vier Dateien komplett ersetzen:
- index.html
- script.js
- admin.html
- admin.js

Alle anderen vorhandenen GitHub-Dateien UNVERAENDERT lassen, insbesondere:
- firebase-config.js
- style.css
- tractor.png
- manifest.json
- analysis.html / analysis.js (falls vorhanden)

Neue Logik:
1. Frischer Firebase-Zeitstempel => ESP32 ONLINE, unabhaengig vom GPS-Fix.
2. gps_valid=false => GPS-Qualitaet zeigt "Kein Fix".
3. Sensorwerte, RSSI, Drehzahl usw. werden trotzdem weiter angezeigt.
4. Die letzte gueltige GPS-Position bleibt in Firebase und auf der Karte.
5. Bei echtem ESP32-Offline bleiben letzte Messwerte und letzte Position sichtbar,
   waehrend der Status eindeutig auf Offline wechselt.
6. Wenn noch nie eine gueltige Position vorhanden war, wird kein falscher
   Standard-Marker angezeigt.

WICHTIG:
Nach dem Hochladen auf GitHub die Website einmal mit Strg+F5 neu laden,
damit sicher script.js V9.5.5 statt einer Browser-Cache-Version verwendet wird.
