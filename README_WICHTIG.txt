MF35X BESUCHER-PASSWORTSCHUTZ V9.5.8 – SELBST ÄNDERBAR

PASSWORT
- Das aktuell eingestellte Besucherpasswort lautet: test
- Das Passwort steht ganz oben in visitor-login.js in dieser Zeile:

  const VISITOR_PASSWORD = "test";

- Zum Ändern ausschließlich den Text zwischen den Anführungszeichen ersetzen.
- Beispiel:

  const VISITOR_PASSWORD = "MeinNeuesPasswort";

- Das Besucherpasswort ist unabhängig vom Adminpasswort.

UPLOAD AUF GITHUB
1. index.html im Hauptverzeichnis des Repositorys vollständig ersetzen.
2. visitor-login.js neu in dasselbe Hauptverzeichnis hochladen.
3. Die vorhandenen Dateien script.js, admin.html, admin.js, style.css,
   firebase-config.js und die ESP32-Dateien NICHT ersetzen.
4. Warten, bis GitHub Pages die Änderung veröffentlicht hat.
5. Die Website einmal mit Strg+F5 neu laden.

FUNKTION
- Vor der Besucheransicht erscheint eine Passwortabfrage.
- Firebase und die Live-Daten werden erst nach korrektem Passwort gestartet.
- Ohne Passwort wird keine Verbindung zu tracker/live oder anderen
  Firebase-Livedaten aufgebaut.
- Nach erfolgreicher Eingabe bleibt die Freigabe in diesem Browser-Tab bis zum
  Schließen des Tabs/Browsers erhalten.
- Über „Abmelden“ wird die Freigabe sofort gelöscht.
- Suchmaschinen erhalten die Anweisung noindex/nofollow.

PASSWORT SPÄTER ÄNDERN
- visitor-login.js auf GitHub öffnen.
- Auf das Stiftsymbol zum Bearbeiten klicken.
- Ganz oben ausschließlich den Wert von VISITOR_PASSWORD ändern.
- Änderung mit „Commit changes“ speichern.
- Durch den eingebauten Cache-Fix wird bei jedem Aufruf die aktuelle Datei
  geladen. index.html muss bei späteren Passwortänderungen nicht geändert werden.
- ESP32, Firebase, index.html und alle anderen Dateien bleiben unverändert.

SICHERHEITSHINWEIS
Dies ist eine wirksame Zugangshürde für normale Besucher und verhindert, dass
ohne Freigabe die Live-Daten automatisch geladen werden. Das Passwort steht wie
das Adminpasswort im öffentlich abrufbaren JavaScript. Ein technisch versierter
Benutzer kann es deshalb finden. Die Sperre ersetzt keine serverseitige
Benutzeranmeldung.
