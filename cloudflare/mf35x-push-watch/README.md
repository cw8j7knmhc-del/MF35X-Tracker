# MF35X Web Push – Mehrgeräte + Sensoralarme

Dieser Worker ist absichtlich vollständig vom ESP32 und der Tracker-Firmware getrennt.

## Aufgaben

- ESP32 Online/Offline weiterhin über den 1-Minuten-Cron überwachen
- innerhalb desselben Cron-Laufs die Firebase-Livewerte alle 10 Sekunden prüfen
- Batterie, Öldruck, Öltemperatur und Zylinderkopftemperatur anhand der vorhandenen Firebase-Grenzwerte bewerten
- Öldruck nur bei Drehzahl >= 400 U/min und nach mindestens 5 s Motorlauf bewerten
- nur bei Zustandswechseln Push senden: Warnung, Alarm und wieder OK
- mehrere Push-Geräte in Cloudflare KV verwalten
- jedes Gerät besitzt einen eigenen Master-Ein/Aus-Schalter
- Meldungsarten pro Gerät einzeln schaltbar

## Sicherheitsgrenzen

Der Worker:
- schreibt nichts in Firebase
- greift nicht auf den ESP32 zu
- verändert keine Tracker-Konfiguration
- verändert weder GPIO11 noch Rennaufzeichnung noch OTA
- enthält keine Firebase-Service-Account-Schlüssel und keine privaten Push-Zugangsdaten im Repository

## Cloudflare-Bindings / Variablen

Öffentliche Variablen:
- `FIREBASE_DB_URL`
- `FIREBASE_PROJECT_ID`
- `OFFLINE_AFTER_MS=45000`
- optional `APP_URL`

KV-Binding:
- `STATE` -> `mf35x-push-state`

Secrets:
- `GOOGLE_SERVICE_ACCOUNT_JSON`
- `FCM_TOKEN` – bestehender Einzelgerät-Fallback während der Migration
- `PUSH_ADMIN_CODE` – Verwaltungscode für Geräte hinzufügen/listen/ein-aus/löschen

## Geräteverwaltung

Die Website ruft ausschließlich die Cloudflare-API auf. Der Verwaltungscode wird vom Nutzer im Adminbereich eingegeben und nur in `sessionStorage` für die laufende Sitzung gehalten.

Pro Gerät werden in KV gespeichert:
- Name
- Plattform
- FCM-Token
- Master `enabled`
- `espStatus`
- `battery`
- `oilPressure`
- `oilTemp`
- `cylinderTemp`

Der FCM-Token wird von der API nie vollständig an den Browser zurückgegeben.

## Cron und Geschwindigkeit

`* * * * *` startet den Worker einmal pro Minute.

Der Worker nutzt innerhalb dieses Scheduled Runs `scheduler.wait(10000)` und prüft die Sensorwerte insgesamt sechs Mal. Dadurch entstehen etwa 10 Sekunden Reaktionszeit für Sensoralarme, ohne einen zusätzlichen ESP32-Sketch und ohne Durable Object.

ESP32 Online/Offline wird weiterhin einmal pro Minute bewertet. Die Offline-Schwelle bleibt 45 Sekunden.

## Cloudflare-KV-Nutzung

Die sechs Sensorprüfungen pro Minute lesen den gespeicherten Alarmzustand aus KV, schreiben ihn aber **nur dann zurück, wenn sich ein relevanter Zustand tatsächlich geändert hat**. Dazu zählen Warn-/Alarm-/OK-Wechsel sowie Start/Stop des Motors für die Öldruckfreigabe.

Wichtig: `updatedAt` darf nicht bei jedem 10-Sekunden-Poll einen KV-Write erzwingen. Der frühere Stand erzeugte dadurch bis zu 8.640 KV-Schreibvorgänge pro Tag und konnte das Free-Tier-Limit sehr schnell überschreiten.

ESP32 Online/Offline wird ebenfalls nur beim ersten Initialisieren bzw. bei einem echten Zustandswechsel in KV geschrieben. Geräte-Registrierung und Änderungen an Push-Einstellungen erzeugen nur bei Benutzeraktionen KV-Writes.

## Verhalten beim ersten Start

Beim ersten Auftreten eines noch unbekannten Sensor- oder ESP32-Zustands wird nur der Ausgangszustand gespeichert. Dadurch entstehen beim Deployment keine unerwünschten Startmeldungen.

## Health-Endpunkt

`/health` zeigt nur technischen Zustand, Zeitstempel und die Anzahl registrierter Geräte. Keine Secrets oder Push-Tokens werden ausgegeben.
