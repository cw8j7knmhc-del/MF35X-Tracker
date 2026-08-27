/* MF35X Tracker – Legacy ESP32 Browser-Benachrichtigung deaktiviert
 *
 * Die echte Mehrgeraete-Pushloesung ueber Cloudflare/FCM ersetzt diese alte
 * lokale Browser-Ueberwachung vollstaendig. Diese Datei bleibt nur als
 * Kompatibilitaets-Shim bestehen, falls ein alter Browser/PWA-Stand sie noch
 * importiert.
 *
 * Wichtig:
 * - keine Firebase-Listener
 * - keine Notification-Aufrufe
 * - kein Admin-Schalter
 * - keine ESP32-/Firmware-Aenderung
 */

const LEGACY_STORAGE_KEY = "mf35xEsp32StatusNotificationsEnabled";

try {
  localStorage.removeItem(LEGACY_STORAGE_KEY);
} catch (error) {
  console.warn("Alte ESP32-Benachrichtigungseinstellung konnte nicht entfernt werden:", error);
}

// Falls ein bereits geladenes Altmodul den alten Block noch in den DOM gesetzt hat,
// wird er bei einem erneuten Import dieses Shims ebenfalls entfernt.
document.getElementById("esp32NotificationSettings")?.remove();
