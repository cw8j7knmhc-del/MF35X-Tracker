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
 * Echte Web-Push-Geraeteverwaltung. Ein Fehler in diesem optionalen Modul darf
 * weder Admin noch Tracker blockieren. Push-Berechtigungen werden nur nach einem
 * bewussten Klick im Adminbereich angefordert.
 */
if (
  typeof window !== "undefined" &&
  /\/admin\.html$/.test(window.location.pathname) &&
  !window.__mf35xWebPushDeviceLoaderStarted
) {
  window.__mf35xWebPushDeviceLoaderStarted = true;

  setTimeout(() => {
    import(`./push-stage-a.js?v=devices-${Date.now()}`).catch(error => {
      console.error("Web-Push-Geraeteverwaltung konnte nicht geladen werden:", error);
    });
  }, 0);
}
