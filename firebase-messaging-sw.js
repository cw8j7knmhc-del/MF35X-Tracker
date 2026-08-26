/* MF35X Tracker – Firebase Cloud Messaging Service Worker */

// Klick auf eine Pushmeldung bringt die bestehende Tracker-Seite in den Vordergrund.
self.addEventListener("notificationclick", event => {
  event.notification.close();

  const targetUrl = new URL("./", self.registration.scope).href;
  event.waitUntil(
    clients.matchAll({ type: "window", includeUncontrolled: true }).then(windows => {
      for (const client of windows) {
        if (client.url.startsWith(self.registration.scope) && "focus" in client) {
          return client.focus();
        }
      }
      return clients.openWindow ? clients.openWindow(targetUrl) : undefined;
    })
  );
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

const messaging = firebase.messaging();

messaging.onBackgroundMessage(payload => {
  const data = payload?.data || {};
  if (data.type !== "esp32_status") return;

  const title = data.title || "MF35X Tracker";
  const options = {
    body: data.body || (data.state === "online" ? "ESP32 ist wieder online." : "ESP32 ist offline."),
    icon: "tractor.png",
    badge: "tractor.png",
    tag: "mf35x-esp32-status",
    renotify: true,
    data: {
      url: new URL("./", self.registration.scope).href,
      state: data.state || "unknown"
    }
  };

  return self.registration.showNotification(title, options);
});
