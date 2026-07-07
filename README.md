# MF35X Live Tracker V9.3.2 Admin

Neu:
- `index.html` Besucheransicht
- `admin.html` Adminseite
- Admin-Passwort in `admin.js`: `mf35x`
- Alarmgrenzen, Maximalwerte und Alarmhistorie liegen zentral in Firebase.

Wichtig:
Der Passwortschutz ist ein einfacher Schutz für GitHub Pages. Für echte Schreibrechte brauchen wir später Firebase Authentication und strengere Firebase-Regeln.


## Fix in V9.3.2

- Admin-Seite verlangt bei jedem neuen Öffnen wieder das Passwort.
- Der automatische Login über sessionStorage wurde entfernt.
- Maximalwerte-Reset wurde korrigiert.
- Beim Reset werden die Maximalwerte auf die aktuell empfangenen Live-Werte gesetzt.
