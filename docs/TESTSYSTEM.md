# MF35X Tracker – Testsystem

## Ziel
Aenderungen an Firmware und Hardwaredefinition sollen vor dem Einsatz am Traktor automatisch auf grundlegende Fehler geprueft werden.

## Stufe 1 – aktiv mit diesem Branch

### 1. Hardware/Firmware-Pinout
`hardware/pinout.csv` ist die kanonische Referenz fuer die aktuell verwendeten GPIO- und ADS1115-Kanaele.

`tests/check_hardware_pinout.py` prueft automatisch:
- ob die Hardware-Symbole im Firmware-Core existieren,
- ob ihre Werte mit `hardware/pinout.csv` uebereinstimmen,
- ob GPIOs doppelt vergeben sind,
- ob reservierte Pins verwendet werden,
- ob GPIO0 frei bleibt.

### 2. ESP32-S3 Compile-Test
Bei Pull Requests und relevanten Aenderungen wird die Firmware mit derselben ESP32-Core-Version wie der signierte OTA-Build kompiliert:
- ESP32 Arduino Core 3.3.11
- Target `esp32:esp32:esp32s3`
- PSRAM `opi`

Der Test signiert und veroeffentlicht keine Firmware. Der bestehende signierte OTA-Workflow bleibt unveraendert.

## Geplante Stufe 2 – Logiktests
Als naechstes werden Hardware-unabhaengige Tests fuer die kritische Steuerlogik aufgebaut:
- GPIO11 Einschaltbedingung Geschwindigkeit + RPM
- RPM-Hysterese
- Plausibilitaetsgrenzen
- ADS1115 Rohwert -> Messwert -> Diagnose
- Alarmgrenzen

## Geplante Stufe 3 – Renn-CSV Replay
Echte Rennaufzeichnungen werden als Regressionstest eingelesen. Nach Firmwareaenderungen sollen Abweichungen bei RPM, GPS, Oeldruck, Ausgang, Alarmen und Rennlogik sichtbar werden.

## Geplante Stufe 4 – KiCad
Sobald ein verbindlicher KiCad-Schaltplan vorhanden ist, wird der Workflow erweitert um:
- KiCad ERC
- Abgleich KiCad-Netze/Pinout mit `hardware/pinout.csv`
- spaeter optional DRC fuer ein PCB

## Sicherheitsprinzip
Test- und Dokumentationsaenderungen werden zuerst ueber einen separaten Branch/Pull Request eingebracht. Die produktive Firmwarelogik wird dabei nicht veraendert.
