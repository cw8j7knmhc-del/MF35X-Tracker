MF35X Tracker Website V9.5.2

Für GitHub komplett ersetzen:
1. admin.html
2. admin.js

Nicht ändern:
- index.html
- script.js
- style.css
- firebase-config.js
- analysis.html
- analysis.js

Neue Firebase-Konfiguration:
tracker/config/external_output/
  speed_enable_kmh = 60
  rpm_on = 3200
  rpm_off = 3150

Logik für die spätere ESP32-Firmware:
- Geschwindigkeit unter speed_enable_kmh -> Ausgang LOW
- Geschwindigkeit >= speed_enable_kmh UND Ausgang LOW UND rpm >= rpm_on -> HIGH
- Ausgang HIGH UND rpm < rpm_off -> LOW
- Fällt die Geschwindigkeit unter speed_enable_kmh -> sofort LOW

Die alte 5-V-Rechteckausgang-Einstellung wurde vollständig aus der Admin-Webseite entfernt.
