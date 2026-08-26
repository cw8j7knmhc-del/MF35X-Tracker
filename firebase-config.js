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
 * Die ESP32-Online/Offline-Benachrichtigung ist bewusst eine reine Admin-Funktion.
 * Sie wird nur auf admin.html geladen. Die Besucheransicht kennt weder den Schalter
 * noch diese Statusueberwachung.
 *
 * admin.html laedt firebase-config.js an mehreren Stellen mit unterschiedlichen
 * Cache-Parametern. Der globale Guard verhindert deshalb einen doppelten Start.
 */
if (
  typeof window !== "undefined" &&
  /\/admin\.html$/.test(window.location.pathname) &&
  !window.__mf35xEsp32StatusLoaderStarted
) {
  window.__mf35xEsp32StatusLoaderStarted = true;

  setTimeout(() => {
    import(`./esp32-status-notifications.js?v=9.5.13-${Date.now()}`).catch(error => {
      console.error("ESP32-Statusbenachrichtigung konnte nicht geladen werden:", error);
    });
  }, 0);
}
