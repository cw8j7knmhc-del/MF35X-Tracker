# MF35X Live Tracker V9.3.3 Admin

Neu:
- `index.html` Besucheransicht
- `admin.html` Adminseite
- Admin-Passwort in `admin.js`: `mf35x`
- Alarmgrenzen, Maximalwerte und Alarmhistorie liegen zentral in Firebase.

Wichtig:
Der Passwortschutz ist ein einfacher Schutz für GitHub Pages. Für echte Schreibrechte brauchen wir später Firebase Authentication und strengere Firebase-Regeln.


## Fix in V9.3.3

- Admin-Login wird nicht mehr per sessionStorage automatisch übersprungen.
- Maximalwerte-Reset schreibt aktiv die aktuell empfangenen Live-Werte nach Firebase.
- Zusätzlich wird `tracker/maxReset` mit Zeitstempel geschrieben.
