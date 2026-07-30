# MF35X Tracker V9.4.0 – einstellbare Aktualisierungsintervalle

## Geändert

Im Adminbereich können jetzt vier Werte eingestellt werden:

- Drehzahl-Ausgang: 5–200 ms, Standard 10 ms
- Drehzahl zu Firebase: 100–5000 ms, Standard 250 ms
- Temperaturen: 250–10000 ms, Standard 1000 ms
- GPS-Position: 1000–30000 ms, Standard 1000 ms

Die Werte werden unter diesem Firebase-Pfad gespeichert:

`tracker/config/intervals`

mit diesen Schlüsseln:

```json
{
  "rpm_output_update_ms": 10,
  "rpm_firebase_update_ms": 250,
  "temperature_update_ms": 1000,
  "gps_update_ms": 1000
}
```

## Dateien auf GitHub Pages ersetzen

- `admin.html`
- `admin.js`
- `index.html`
- `script.js`
- `style.css`

Die übrigen vorhandenen Dateien wie `firebase-config.js`, `tractor.png` und `manifest.json` bleiben bestehen.

## Firebase

Beim ersten erfolgreichen Admin-Login legt `admin.js` den Pfad
`tracker/config/intervals` automatisch mit den Standardwerten an, falls er noch nicht existiert.

Das funktioniert nur, wenn die bestehenden Firebase-Regeln Schreibzugriff auf
`tracker/config/intervals` erlauben. Da keine Firebase-Regeldatei vorlag, wurde daran nichts verändert.

## Wichtig zum ESP32

Die Website und Firebase sind damit vorbereitet. Der ESP32 übernimmt diese Werte erst,
nachdem der Sketch einmalig um das Lesen von `tracker/config/intervals` und um getrennte
Zeitgeber erweitert wurde.
