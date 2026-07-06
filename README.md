# MF35X Live Tracker V8

## Neu gegenüber V7

- Drehzahl-Verlauf entfernt
- Öldruck-Verlauf entfernt
- Zylindertemperatur-Verlauf hinzugefügt
- Öltemperatur-Verlauf bleibt erhalten
- Eigenes Traktorbild als Karten-Marker und Header-Symbol eingebaut

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
