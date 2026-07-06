# MF35X Live Tracker V6

## Änderungen

- Font-Awesome-Icons statt Emoji-Icons
- Drehzahl mit `U/min`
- Drehzahl-Icon: Tacho/Gauge
- Öldruck-Icon: Ölkanne
- Keine Runden-/Fahrtenprotokoll-Funktion
- Einstellbare Alarmgrenzen direkt auf der Webseite
- Alarmgrenzen werden lokal im Browser gespeichert

## Alarmgrenzen

Aktuell vorbereitet für:

- Batteriespannung
- Öldruck
- Öltemperatur
- Zylindertemperatur

## Erwartete Firebase-Felder

Pfad:

`tracker/live`

```json
{
  "lat": 48.208174,
  "lng": 16.373819,
  "speed_kmh": 12.4,
  "satellites": 13,
  "battery_v": 13.8,
  "rpm": 2100,
  "oil_pressure": 4.2,
  "oil_temp": 92,
  "cylinder_temp": 145
}
```
