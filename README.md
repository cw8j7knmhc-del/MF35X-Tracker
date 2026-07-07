# MF35X Live Tracker V9.3.4 Admin

Neu:
- `index.html` Besucheransicht
- `admin.html` Adminseite
- Admin-Passwort in `admin.js`: `mf35x`
- Alarmgrenzen, Maximalwerte und Alarmhistorie liegen zentral in Firebase.

Wichtig:
Der Passwortschutz ist ein einfacher Schutz für GitHub Pages. Für echte Schreibrechte brauchen wir später Firebase Authentication und strengere Firebase-Regeln.


## Fix in V9.3.4

- Der Reset-Button ist jetzt blau und deutlicher sichtbar.
- Maximalwerte-Reset sendet zusätzlich einen Reset-Befehl an alle geöffneten Besucher-Seiten.
- Dadurch können geöffnete Besucher-Seiten den Reset nicht mehr sofort mit alten Maximalwerten überschreiben.
- Admin-Login wird nicht automatisch übersprungen.
