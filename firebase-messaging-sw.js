/* MF35X Tracker V9.6.0 – Firebase Messaging Service Worker */

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

importScripts("https://www.gstatic.com/firebasejs/12.17.1/firebase-app-compat.js");
importScripts("https://www.gstatic.com/firebasejs/12.17.1/firebase-messaging-compat.js");

firebase.initializeApp({
  apiKey: "AIzaSyARYH1P-dQ8-tbp4BnhcThHqqNuLaZmUxU",
  authDomain: "tracker-989a9.firebaseapp.com",
  databaseURL: "https://tracker-989a9-default-rtdb.europe-west1.firebasedatabase.app",
  projectId: "tracker-989a9",
  storageBucket: "tracker-989a9.firebasestorage.app",
  messagingSenderId: "523207717217",
  appId: "1:523207717217:web:b7ea5a4e90b0a653aed520"
});

const messaging = firebase.messaging();

messaging.onBackgroundMessage(payload => {
  const data = payload.data || {};
  const title = data.title || "MF35X Tracker";
  const body = data.body || "Neue Meldung";

  return self.registration.showNotification(title, {
    body,
    icon: "./tractor.png",
    badge: "./tractor.png",
    tag: data.tag || "mf35x-alarm",
    renotify: true,
    requireInteraction: data.level === "alarm",
    data: {
      url: new URL("./", self.registration.scope).href,
      eventId: data.eventId || ""
    }
  });
});
