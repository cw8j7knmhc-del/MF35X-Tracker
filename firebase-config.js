const firebaseConfig = {
  apiKey: "AIzaSyARYH1P-dQ8-tbp4BnhcThHqqNuLaZmUxU",
  authDomain: "tracker-989a9.firebaseapp.com",
  databaseURL: "https://tracker-989a9-default-rtdb.europe-west1.firebasedatabase.app",
  projectId: "tracker-989a9",
  storageBucket: "tracker-989a9.firebasestorage.app",
  messagingSenderId: "523207717217",
  appId: "1:523207717217:web:b7ea5a4e90b0a653aed520"
};

export { firebaseConfig };

/*
 * Die ESP32-Online/Offline-Einstellung ist bewusst eine reine Admin-Funktion.
 * Nur auf admin.html wird der Schalter angezeigt und kann verändert werden.
 *
 * Wenn die Funktion auf diesem Browser/PWA im Admin aktiviert wurde, darf die
 * eigentliche Überwachung anschließend auch unsichtbar in der Besucheransicht
 * weiterlaufen. Dadurch bleiben Meldungen beim Wechsel vom Admin zur Liveansicht aktiv.
 *
 * admin.html lädt firebase-config.js an mehreren Stellen mit unterschiedlichen
 * Cache-Parametern. Der globale Guard verhindert deshalb einen doppelten Admin-Start.
 */
if (
  typeof window !== "undefined" &&
  /\/admin\.html$/.test(window.location.pathname) &&
  !window.__mf35xEsp32StatusLoaderStarted
) {
  window.__mf35xEsp32StatusLoaderStarted = true;

  setTimeout(() => {
    import(`./esp32-status-notifications.js?v=9.5.14-${Date.now()}`).catch(error => {
      console.error("ESP32-Statusbenachrichtigung konnte nicht geladen werden:", error);
    });
  }, 0);
}
