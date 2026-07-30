# MF35X Tracker V9.4.1

## Einstellbare Intervalle

- 5-V-Drehzahl-Rechteckausgang: Standard 10 ms
- Drehzahl zu Firebase/Website: Standard 250 ms
- Öldruck zu Firebase/Website: Standard 100 ms
- Temperaturen: Standard 1000 ms
- GPS-Position und GPS-Geschwindigkeit: Standard 1000 ms

## Firebase-Pfad

`tracker/config/intervals`

```json
{
  "rpm_output_update_ms": 10,
  "rpm_firebase_update_ms": 250,
  "oil_pressure_update_ms": 100,
  "temperature_update_ms": 1000,
  "gps_update_ms": 1000
}
```

Der Rechteckausgang läuft kontinuierlich. Sein Wert bestimmt nur, wie oft die
Ausgangsfrequenz an die neu berechnete Drehzahl angepasst wird.

GPS-Position und GPS-Geschwindigkeit werden gemeinsam aktualisiert.

## Auf GitHub Pages ersetzen

- admin.html
- admin.js
- index.html
- script.js
- style.css

Der ESP32-Sketch muss danach einmalig an die neue Firebase-Konfiguration angepasst werden.
