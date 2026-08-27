# MF35X Web Push – Stufe B

Dieser Worker ist absichtlich vollständig vom ESP32 und der Tracker-Firmware getrennt.

## Aufgabe

- alle 1 Minute `tracker/live/timestamp` aus Firebase RTDB lesen
- bei mindestens 45 s ohne neue Live-Daten: Zustand `offline`
- bei wieder frischen Live-Daten: Zustand `online`
- nur bei einem Zustandswechsel eine echte FCM-Web-Push-Nachricht senden
- letzten Zustand in Cloudflare KV speichern

## Sicherheitsgrenzen

Der Worker:
- schreibt nichts in Firebase
- greift nicht auf den ESP32 zu
- verändert keine Konfiguration
- verändert weder GPIO11 noch Rennaufzeichnung noch OTA
- enthält keine Firebase-Service-Account-Schlüssel und keinen FCM-Gerätetoken im Repository

## Cloudflare-Bindings / Variablen

Öffentliche Variablen:
- `FIREBASE_DB_URL`
- `FIREBASE_PROJECT_ID`
- `OFFLINE_AFTER_MS=45000`
- optional `APP_URL`

KV-Binding:
- `STATE`

Secrets:
- `GOOGLE_SERVICE_ACCOUNT_JSON`
- `FCM_TOKEN`

## Cron

`* * * * *` = einmal pro Minute.

Bei 45 s Offline-Schwelle ergibt sich typischerweise etwa 1–1,5 Minuten
bis zur Offline-Meldung, abhängig davon, wann innerhalb der Minute der
Verbindungsabbruch stattfindet.

## Verhalten beim ersten Start

Der erste Cron-Lauf speichert nur den aktuellen Zustand. Er sendet
absichtlich noch keine Nachricht. Erst ein späterer Zustandswechsel
erzeugt Push.

## Health-Endpunkt

`/health` zeigt nur technischen Zustand und Zeitstempel an. Keine Secrets
werden ausgegeben.
