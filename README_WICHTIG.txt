MF35X BESUCHER-PASSWORTSCHUTZ V9.5.7

PASSWORT
- Das von dir gewählte Besucherpasswort ist bereits eingetragen.
- Es wird in visitor-login.js nur als SHA-256-Prüfwert gespeichert.
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
- Nur visitor-login.js muss mit dem neuen Passwort-Prüfwert ersetzt werden.
- ESP32, Firebase, index.html und alle anderen Dateien bleiben unverändert.

SICHERHEITSHINWEIS
Dies ist eine wirksame Zugangshürde für normale Besucher und verhindert, dass
ohne Freigabe die Live-Daten automatisch geladen werden. Da GitHub Pages eine
öffentliche statische Website ist, ersetzt diese Sperre keine serverseitige
Benutzeranmeldung.
