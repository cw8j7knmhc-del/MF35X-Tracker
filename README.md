# MF35X Tracker V9.4.6

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


## Alarmstart-Fix V9.4.6

Beim Öffnen der Besucheransicht werden Alarme erst geprüft, wenn sowohl

- die gespeicherten Alarmgrenzen aus `tracker/settings`
- als auch gültige Live-Daten aus `tracker/live`

geladen wurden.

Dadurch erscheint beim Seitenstart kein kurzzeitiger falscher Öldruckalarm und es
wird auch kein falscher Eintrag in der Alarmhistorie erzeugt.


## Öldruckalarm-Fix V9.4.6

Der Öldruckalarm wird nur ausgewertet, wenn:

- eine gültige Drehzahl vorhanden ist,
- die Drehzahl mindestens 400 U/min beträgt,
- und der Motor bereits mindestens 5 Sekunden über 400 U/min läuft.

Bei `rpm = null`, `--- U/min`, `0 U/min` oder weniger als 400 U/min wird kein
Öldruckalarm angezeigt und kein entsprechender Eintrag in der Alarmhistorie erzeugt.

Damit bleibt die Anzeige bei ausgeschaltetem Motor neutral, obwohl der gemessene
Öldruck korrekt 0,0 bar beträgt.


## Layout V9.4.6

Obere Reihe:
1. Geschwindigkeit
2. Drehzahl
3. Zylindertemperatur
4. Öltemperatur
5. Öldruck

Untere Reihe:
1. Batteriespannung
2. Status
3. Verbindung
4. GPS-Qualität
5. Satelliten

Das Benachrichtigungs- und Adminfeld befindet sich als letztes Bedienfeld am Seitenende.


## Frische Live-Daten V9.4.6

Die Besucheransicht prüft jetzt den Firebase-Zeitstempel `tracker/live/timestamp`,
bevor Werte angezeigt werden.

- Alte gespeicherte Werte werden beim Öffnen nicht mehr als aktuelle Werte angezeigt.
- Das tatsächliche Updateintervall wird aus `tracker/device/uploadIntervalMs` gelesen.
- Offline-Timeout = vier Uploadintervalle, mindestens 1,5 Sekunden.
- Bei aktuell 500 ms Uploadintervall beträgt der Timeout 2 Sekunden.
- Die Anzeige „Letztes Update“ verwendet den echten Firebase-Zeitstempel.
- Verbindung und RSSI werden bei Offline ebenfalls geleert.
