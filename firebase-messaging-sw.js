/* MF35X Web Push – Stufe A
 * Reiner Firebase-Messaging-Service-Worker.
 * WICHTIG: Kein fetch-Handler, kein Cache, keine Änderung an Website-/Tracker-Requests.
 */

self.addEventListener("notificationclick", event => {
  event.notification.close();

  const target = new URL("./", self.registration.scope).href;
  event.waitUntil((async () => {
    const windows = await clients.matchAll({ type: "window", includeUncontrolled: true });
    for (const client of windows) {
      if (client.url.startsWith(self.registration.scope) && "focus" in client) {
        return client.focus();
      }
    }
    return clients.openWindow(target);
  })());
});

importScripts("https://www.gstatic.com/firebasejs/10.12.2/firebase-app-compat.js");
importScripts("https://www.gstatic.com/firebasejs/10.12.2/firebase-messaging-compat.js");

firebase.initializeApp({
  apiKey: "AIzaSyARYH1P-dQ8-tbp4BnhcThHqqNuLaZmUxU",
  authDomain: "tracker-989a9.firebaseapp.com",
  databaseURL: "https://tracker-989a9-default-rtdb.europe-west1.firebasedatabase.app",
  projectId: "tracker-989a9",
  storageBucket: "tracker-989a9.firebasestorage.app",
  messagingSenderId: "523207717217",
  appId: "1:523207717217:web:b7ea5a4e90b0a653aed520"
});

// Für Stufe A genügt die FCM-Initialisierung. Testnachrichten aus der Firebase
// Console mit Notification-Payload werden vom Messaging-SDK im Hintergrund
// angezeigt. Eine eigene Alarm-/Trackerlogik kommt ausdrücklich erst in Stufe B.
firebase.messaging();
