# MF35X Web Push – Teststand V9.6.0

Dieser Stand liegt ausschließlich im Branch `feature/web-push`. `main` bleibt unverändert.

## Ziel

Echte Push-Benachrichtigungen auf der iPhone-Home-Screen-PWA, auch wenn die Tracker-Web-App nicht geöffnet ist.

## Architektur

1. ESP32 erkennt neue Warn-/Alarmstufen lokal.
2. Nur Zustandswechsel werden nach `tracker/pushEvents` geschrieben.
3. Firebase Cloud Function `sendMf35xPush` reagiert auf neue Events.
4. FCM sendet an registrierte Firebase Installation IDs (FIDs).
5. `firebase-messaging-sw.js` zeigt die Meldung im Hintergrund an.

## Vor Aktivierung erforderlich

### 1. Web-Push-Schlüsselpaar erzeugen

Firebase Console → Projekteinstellungen → Cloud Messaging → Web-Konfiguration → Web Push certificates → Generate Key Pair.

Nur den dort angezeigten **öffentlichen** Schlüssel in `push-config.js` eintragen. Niemals einen privaten Schlüssel in GitHub speichern.

### 2. Cloud Functions

Das Firebase-Projekt muss für das Deployment von Cloud Functions im Blaze-Tarif sein. Anschließend aus dem Repository-Root:

```bash
npm install -g firebase-tools
firebase login
cd functions
npm install
cd ..
firebase deploy --only functions
```

### 3. Website-Test

Erst den Feature-Branch testen bzw. gezielt in `main` übernehmen, wenn VAPID + Function eingerichtet sind.

Auf dem iPhone muss die Seite als Home-Screen-Web-App installiert sein. Nach Besucher-Login einmal `Browser erlauben` bzw. den Benachrichtigungsschalter aktivieren.

## Wichtig

Der bisherige Vordergrund-Mechanismus bleibt im alten `script.js` vorhanden, wird aber vom neuen Push-Client durch einen separaten lokalen Schalter deaktiviert. Dadurch entstehen keine doppelten lokalen Alarmmeldungen.

Der ESP32-Teststand ist separat zu halten und erst nach erfolgreichem Website-/Function-Test zu flashen.
