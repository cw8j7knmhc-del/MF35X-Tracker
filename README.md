# MF35X Live Tracker V9

## Neu gegenüber V8

- Maximalwerte ergänzt
- Alarmhistorie ergänzt
- Maximalwerte und Alarmhistorie werden lokal im Browser gespeichert
- Reset-Button für Maximalwerte
- Leeren-Button für Alarmhistorie

## Maximalwerte

Erfasst werden:

- Max. Geschwindigkeit
- Max. Drehzahl
- Max. Öltemperatur
- Max. Zylindertemperatur
- Min. Öldruck
- Min. Batteriespannung

## Alarmhistorie

Neue Alarme werden beim ersten Auftreten eingetragen.
Wenn ein Alarm dauerhaft anliegt, wird er nicht sekündlich neu eingetragen.

## Dateien

Diese Dateien müssen in GitHub liegen:

- `index.html`
- `style.css`
- `script.js`
- `README.md`
- `tractor.png`

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
  "cylinder_temp": 145,
  "hdop": 1.2,
  "wifi_rssi": -58
}
```
