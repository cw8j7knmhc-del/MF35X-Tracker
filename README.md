# MF35X Live Tracker

Diese Webseite liest die GPS-Daten aus Firebase Realtime Database und zeigt sie live auf einer OpenStreetMap-Karte an.

## Dateien

- `index.html` – Webseite
- `style.css` – Design
- `script.js` – Firebase + Karte

## Start am PC

1. ZIP entpacken
2. `index.html` doppelt anklicken
3. Falls Browser-Sicherheitsprobleme auftreten: mit VS Code + Live Server öffnen

## Firebase-Pfad

Die Webseite liest aus:

`tracker/live`

Der ESP32 sollte dort z. B. diese Daten schreiben:

```json
{
  "lat": 48.208174,
  "lng": 16.373819,
  "speed_kmh": 12.4,
  "satellites": 15,
  "online": true
}
```

## Später

Für weltweiten Zugriff kann diese Webseite kostenlos über GitHub Pages veröffentlicht werden.
