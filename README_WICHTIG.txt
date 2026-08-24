MF35X ADMIN – PASSWORT-/CACHE-FIX V9.5.9

UPLOAD AUF GITHUB
1. Nur die beiliegende Datei admin.html in das Hauptverzeichnis des
   GitHub-Repositories hochladen.
2. Die dort vorhandene admin.html vollständig ersetzen.
3. Die bestehende admin.js NICHT ersetzen. Dein dort bereits eingetragenes
   neues Passwort bleibt dadurch erhalten.
4. Warten, bis GitHub Pages die Änderung veröffentlicht hat.
5. Die Adminseite einmal mit Strg+F5 neu laden.

WAS WURDE KORRIGIERT?
- Der fest eingetragene Hinweis „Standardpasswort: mf35x“ wurde entfernt.
- admin.js wird bei jedem Aufruf mit einer neuen Cache-Kennung geladen.
- Änderungen an ADMIN_PASSWORD in admin.js werden dadurch zuverlässig geladen.
- An den Adminfunktionen, Firebase-Pfaden und ESP32-Funktionen wurde nichts
  geändert.

PASSWORT ÄNDERN
In der bestehenden admin.js weiterhin nur diese Zeile ändern:

const ADMIN_PASSWORD = "DEIN_NEUES_PASSWORT";

HINWEIS
Das Passwort liegt weiterhin im öffentlich abrufbaren JavaScript und ist daher
nur ein einfacher Schutz der Bedienoberfläche, keine echte sichere Anmeldung.
