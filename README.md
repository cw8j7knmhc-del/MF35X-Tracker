# MF35X Live Tracker V4

## Enthalten

- Modernes Dashboard mit Visualisierungen
- Status mit LED-Punkt
- Geschwindigkeit
- Satelliten
- Letztes Update
- Batteriespannung
- Drehzahl in u/min
- Öldruck
- Öltemperatur
- Zylindertemperatur
- OpenStreetMap
- Google-Maps-Link

## Erwartete Firebase-Felder

Pfad:

`tracker/live`

Felder:

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

Nicht vorhandene Werte werden als `---` angezeigt.
