# MF35X Live Tracker V9.3.1 Admin

Neu:
- `index.html` Besucheransicht
- `admin.html` Adminseite
- Admin-Passwort in `admin.js`: `mf35x`
- Alarmgrenzen, Maximalwerte und Alarmhistorie liegen zentral in Firebase.

Wichtig:
Der Passwortschutz ist ein einfacher Schutz für GitHub Pages. Für echte Schreibrechte brauchen wir später Firebase Authentication und strengere Firebase-Regeln.


## Fix in V9.3.1

- Maximalwerte-Reset im Adminbereich wurde angepasst.
- Beim Reset werden die Maximalwerte nicht mehr leer gesetzt, sondern auf die aktuell empfangenen Live-Werte zurückgesetzt.
- Dadurch erscheinen nach dem Wechsel in die Besucheransicht nicht wieder die alten Maximalwerte.
