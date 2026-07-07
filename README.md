# MF35X Live Tracker V9.4 Admin

Neu:
- `index.html` Besucheransicht
- `admin.html` Adminseite
- Admin-Passwort in `admin.js`: `mf35x`
- Alarmgrenzen, Maximalwerte und Alarmhistorie liegen zentral in Firebase.

Wichtig:
Der Passwortschutz ist ein einfacher Schutz für GitHub Pages. Für echte Schreibrechte brauchen wir später Firebase Authentication und strengere Firebase-Regeln.


## Neu in V9.4

- Live-Route auf der Karte
- Grüne Fahrspur hinter dem Traktor
- Route kann lokal im Browser gelöscht werden
- Besucher haben weiterhin keine Admin-Funktionen

Hinweis:
Die Route wird aktuell lokal im geöffneten Browser aufgezeichnet.
Wenn die Seite neu geladen wird, startet die Route neu.
Für eine zentrale gespeicherte Rennroute bauen wir später den ESP32-Code so um,
dass er die Route direkt nach Firebase schreibt.
