# MF35X Livetracker – signiertes Online-Firmwareupdate

## Basis

- Firmware-Basis: **V5.9.7 GPS 10Hz SAFE**
- Neue OTA-Firmware: **V5.9.8**
- Website-Basis: **V9.5.6**
- Neue Admin-Seite: **V9.5.7**
- Rückfallstand bleibt: **V5.9.5 Final**
- V5.9.6 wird nicht verwendet.

Die bestätigte GPS-Funktion aus V5.9.7 bleibt erhalten: Auto-Baud, 10 Hz, 4096 Byte RX-Puffer, eigener FreeRTOS-GPS-Task und Beibehalten der letzten gültigen Position.

## Einmalige Erstinstallation

V5.9.7 kann sich noch nicht selbst aktualisieren. Deshalb muss **V5.9.8 einmal per USB** auf den ESP32 geladen werden.

Wichtig: Eine Partitionstabelle mit **zwei OTA-App-Partitionen plus otadata** verwenden. Keinen Modus wählen, der nur eine einzige große APP-Partition besitzt.

Beim ersten V5.9.8-Upload **nicht den gesamten Flash/NVS löschen**. V5.9.8 verwendet im Normalfall die bereits vom bisherigen V5.9.7 gespeicherten WLAN-Zugangsdaten. Dadurch müssen WLAN-Passwörter nicht im öffentlichen GitHub-Quellcode stehen.

Falls NVS bereits gelöscht wurde:
1. `secrets.example.h` als `secrets.h` kopieren.
2. Eigenen WLAN-Namen und WLAN-Passwort eintragen.
3. Lokal per USB hochladen.
4. `secrets.h` niemals zu GitHub hochladen.

## Privater OTA-Schlüssel

Der zu diesem Paket gehörende private RSA-Schlüssel muss getrennt und geheim aufbewahrt werden.

In GitHub einmalig:
1. Repository `MF35X-Tracker` öffnen.
2. `Settings` → `Secrets and variables` → `Actions`.
3. `New repository secret`.
4. Name exakt: `OTA_SIGNING_KEY`.
5. Den kompletten Inhalt der privaten PEM-Datei einfügen.

Der private Schlüssel darf **nie** als normale Datei in GitHub liegen.

## Danach online ändern

1. In GitHub `esp32/MF35X_Livetracker/` öffnen.
2. Sketch oder andere Dateien im Browser ändern.
3. Für jede neue Firmware die Version in `firmware_version.h` erhöhen, z. B.:
   - `V5.9.8` → `V5.9.9`
   - `50908` → `50909`
4. Änderung auf `main` speichern/committen.
5. GitHub Actions kompiliert mit Arduino-ESP32 **3.3.10**, signiert das Binary und aktualisiert `firmware/manifest.json` und `firmware/MF35X_Livetracker_signed.bin`. Der ESP32 liest diese Dateien direkt aus dem `main`-Branch; die GitHub-Pages-Veröffentlichung muss dafür nicht abgewartet werden.
6. Admin-Seite öffnen → `Nach Update suchen`.
7. `Firmware aktualisieren` drücken.
8. ESP32 lädt die neue Firmware selbst über RUT200/WLAN, prüft MD5 und RSA/SHA-256-Signatur, schreibt die zweite OTA-Partition und startet neu.

## Rollback-Schutz

Die neue Firmware wird nach einem OTA-Boot nicht sofort endgültig bestätigt. V5.9.8 verschiebt die Bestätigung um 30 Sekunden. Läuft die neue Firmware stabil, wird sie als gültig markiert. Bei einem Boot-Fehler vor dieser Bestätigung kann der ESP32 bei einer passenden OTA-Partitionstabelle auf die vorherige App zurückfallen.

## Sicherheitsregeln

- OTA-Binary-URL ist fest im ESP32 hinterlegt; Firebase kann keine fremde URL vorgeben.
- Nur Firmware mit dem passenden privaten RSA-Schlüssel wird akzeptiert.
- Der öffentliche Schlüssel darf im Repository liegen; der private Schlüssel nicht.
- `secrets.h` und `*.pem` sind durch `.gitignore` ausgeschlossen.
- Das bestehende clientseitige Admin-Passwort ersetzt keine echte Firebase-Authentifizierung. Die Signaturprüfung verhindert trotzdem, dass über den OTA-Befehl eine nicht autorisierte Firmware installiert wird.
