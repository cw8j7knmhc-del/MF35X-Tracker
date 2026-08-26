# MF35X ESP32 Online/Offline – echter Hintergrund-Push

Stand: vorbereitet auf Branch `feature/esp32-background-push`.

## Ziel

Der ESP32-Status wird serverseitig überwacht. Bei einem echten Zustandswechsel wird eine Pushmeldung an registrierte Browser/PWAs geschickt – auch wenn die Tracker-Seite nicht geöffnet ist.

Meldungen:

- `ESP32 ist offline – es kommen keine aktuellen Statusdaten mehr an.`
- `ESP32 ist wieder online.`

Die normalen Sensor-/Alarm-Browsermeldungen bleiben davon unabhängig.

## Architektur

1. Der vorhandene ESP32 schreibt weiterhin seinen Status nach `tracker/device/timestamp`.
2. `monitorEsp32Status` prüft den Zeitstempel einmal pro Minute.
3. Nach mehr als 35 Sekunden ohne aktuellen Heartbeat gilt der ESP32 als offline.
4. Nur bei einem Zustandswechsel (`online -> offline` bzw. `offline -> online`) wird Firebase Cloud Messaging ausgelöst.
5. `firebase-messaging-sw.js` empfängt die Meldung auch im Hintergrund.
6. Der Schalter `ESP32 Online/Offline Push` registriert bzw. entfernt das jeweilige Gerät.

Hinweis: Durch den minutenweisen Cloud-Scheduler kommt die Offline-/Online-Meldung typischerweise innerhalb von etwa 1 bis 2 Minuten. Das verhindert gleichzeitig Fehlalarme durch sehr kurze LTE-Unterbrechungen.

## Bereits vorbereitet

- `firebase-messaging-sw.js`
- `push-client.js`
- `push-config.js`
- `functions/index.js`
- `functions/package.json`
- `firebase.json`
- `.firebaserc`
- zusätzlicher Push-Schalter in `index.html`

## Einmalig in Firebase erforderlich

### 1. Web-Push-Schlüssel erzeugen

Firebase Console -> Projekteinstellungen -> Cloud Messaging -> Web-Konfiguration / Web Push certificates -> Schlüsselpaar erzeugen.

Nur den dort angezeigten **öffentlichen** Schlüssel in `push-config.js` eintragen:

```js
export const FIREBASE_WEB_PUSH_PUBLIC_KEY = "HIER_DER_OEFFENTLICHE_SCHLUESSEL";
```

Keinen privaten Schlüssel in GitHub speichern.

### 2. Cloud Functions freischalten und deployen

Für geplante Cloud Functions muss das Firebase-Projekt die entsprechenden Cloud-/Billing-Voraussetzungen erfüllen (in der Praxis Blaze-Tarif).

Danach im Repository:

```bash
npm install -g firebase-tools
firebase login
cd functions
npm install
cd ..
firebase deploy --only functions
```

Deployt werden:

- `registerPushDevice`
- `unregisterPushDevice`
- `monitorEsp32Status`

## Test

1. Funktionen deployen.
2. Öffentlichen VAPID-Schlüssel in `push-config.js` eintragen.
3. Feature-Branch auf die Testseite bringen bzw. nach erfolgreichem Test mergen.
4. Tracker-Seite auf dem Zielgerät öffnen.
5. `ESP32 Online/Offline Push` einschalten und Browserfreigabe bestätigen.
6. ESP32 zunächst online lassen, damit der Monitor seinen Ausgangszustand initialisiert.
7. ESP32 ausschalten und auf die Offline-Pushmeldung warten.
8. ESP32 wieder einschalten und die Online-Pushmeldung kontrollieren.

## Sicherheit

- Der private Web-Push-Schlüssel wird von Firebase verwaltet und gehört nicht ins Repository.
- Registrierungstoken werden nur über Cloud Functions registriert/entfernt.
- Der produktive `main`-Branch bleibt bis zur Firebase-Aktivierung unverändert.
