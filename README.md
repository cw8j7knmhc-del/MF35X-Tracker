# MF35X Live Tracker V7 NEU

Wenn du nach dem Hochladen noch V6 siehst, öffne die Seite mit:

`https://cw8j7knmhc-del.github.io/MF35X-Tracker/?v=7`

und lade mit Strg + F5 neu.

## Enthalten

- Version-7-Markierung oben sichtbar
- Alarmbanner
- Trenddiagramme für Drehzahl, Öldruck, Öltemperatur
- GPS-Qualität über `hdop`
- WLAN-/Verbindungsstatus über `wifi_rssi`
- Kein Nachtmodus
- Kein eigenes Traktorsymbol
- Kein Runden-/Fahrtenprotokoll

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
