MF35X TRACKER – ADMIN V9.5.8 RENNAUSWERTUNG
================================================

Basis:
- Firmware bleibt unverändert: V5.9.9 OTA SIGNED
- Besucheransicht bleibt funktional unverändert.
- Änderung betrifft nur die Website/Admin-Funktion.

NEU
1. Rennauswertung ist direkt in admin.html integriert.
2. Rennlisten und Historiedaten werden erst nach erfolgreichem Admin-Login geladen.
3. Die bisherige öffentliche analysis.html zeigt keine Renndaten mehr und verweist zum Admin-Login.
4. analysis.js enthält keine Firebase-Auswertung mehr.
5. Gespeicherte Rennaufzeichnungen können im Admin-Bereich gelöscht werden.
6. Das Löschen entfernt atomar:
   - tracker/races/<Renn-ID>
   - tracker/history/<Renn-ID>
7. Eine aktuell laufende Aufzeichnung kann nicht gelöscht werden.
8. Vor dem Löschen erscheint eine eindeutige Sicherheitsabfrage.

DATEIEN FÜR GITHUB
- admin.html
- admin.js
- analysis.html
- analysis.js

Alle vier Dateien in die Wurzel des GitHub-Repositories hochladen und vorhandene Dateien ersetzen.
Es ist KEIN ESP32-/OTA-Firmwareupdate nötig, da esp32/ nicht geändert wird.

WICHTIG ZUR ZUGRIFFSSICHERHEIT
Die derzeitige Admin-Anmeldung arbeitet weiterhin mit dem bestehenden clientseitigen Passwort.
Damit ist die Rennauswertung in der normalen Website-Bedienung nur im Admin-Bereich sichtbar,
aber dies ist KEINE kryptografisch echte Zugriffskontrolle. Für einen technisch wirklich geschützten
Zugriff wären Firebase Authentication und passende Realtime-Database-Regeln erforderlich.
