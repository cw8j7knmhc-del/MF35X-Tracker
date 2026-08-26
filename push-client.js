/* MF35X Tracker – echter Hintergrund-Push fuer ESP32 Online/Offline */
import {
  getApps,
  getApp,
  initializeApp
} from "https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js";
import {
  getMessaging,
  getToken,
  deleteToken,
  isSupported,
  onMessage
} from "https://www.gstatic.com/firebasejs/10.12.2/firebase-messaging.js";
import {
  getFunctions,
  httpsCallable
} from "https://www.gstatic.com/firebasejs/10.12.2/firebase-functions.js";
import { firebaseConfig } from "./firebase-config.js";
import { FIREBASE_WEB_PUSH_PUBLIC_KEY } from "./push-config.js";

const ENABLED_KEY = "mf35xBackgroundPushEnabled";
const TOKEN_KEY = "mf35xBackgroundPushToken";

const app = getApps().length ? getApp() : initializeApp(firebaseConfig);
const functions = getFunctions(app, "europe-west1");
const registerPushDevice = httpsCallable(functions, "registerPushDevice");
const unregisterPushDevice = httpsCallable(functions, "unregisterPushDevice");

let messaging = null;
let supported = false;
let busy = false;

const toggle = document.getElementById("backgroundPushToggle");
const statusEl = document.getElementById("backgroundPushStatus");

setup().catch(error => {
  console.error("MF35X Background-Push konnte nicht initialisiert werden:", error);
  setStatus("Fehler");
});

async function setup() {
  if (!toggle || !statusEl) return;

  supported = await isSupported();
  if (!supported || !("serviceWorker" in navigator) || !("Notification" in window)) {
    toggle.checked = false;
    toggle.disabled = true;
    setStatus("Nicht unterstützt");
    return;
  }

  if (!FIREBASE_WEB_PUSH_PUBLIC_KEY) {
    toggle.checked = false;
    toggle.disabled = true;
    setStatus("Noch nicht eingerichtet");
    return;
  }

  messaging = getMessaging(app);

  const wanted = localStorage.getItem(ENABLED_KEY) === "true";
  toggle.checked = wanted && Notification.permission === "granted";
  setStatus(toggle.checked ? "Ein" : "Aus");

  toggle.addEventListener("change", async () => {
    if (busy) return;
    busy = true;
    toggle.disabled = true;

    try {
      if (toggle.checked) {
        await enableBackgroundPush(true);
      } else {
        await disableBackgroundPush();
      }
    } catch (error) {
      console.error("MF35X Background-Push Fehler:", error);
      toggle.checked = false;
      localStorage.setItem(ENABLED_KEY, "false");
      setStatus("Fehler");
    } finally {
      busy = false;
      toggle.disabled = false;
    }
  });

  // Wenn der Nutzer Push bereits aktiviert hat und die Browserfreigabe noch
  // besteht, wird die Registrierung beim naechsten Seitenstart automatisch erneuert.
  if (wanted && Notification.permission === "granted") {
    try {
      await enableBackgroundPush(false);
    } catch (error) {
      console.warn("MF35X Push-Registrierung konnte nicht erneuert werden:", error);
      toggle.checked = false;
      setStatus("Neu aktivieren");
    }
  }

  onMessage(messaging, payload => {
    const data = payload?.data || {};
    if (data.type !== "esp32_status") return;
    if (localStorage.getItem(ENABLED_KEY) !== "true") return;
    if (Notification.permission !== "granted") return;

    new Notification(data.title || "MF35X Tracker", {
      body: data.body || (data.state === "online" ? "ESP32 ist wieder online." : "ESP32 ist offline."),
      icon: "tractor.png",
      tag: "mf35x-esp32-status"
    });
  });
}

async function enableBackgroundPush(askPermission) {
  if (!messaging) throw new Error("Firebase Messaging ist nicht initialisiert.");

  let permission = Notification.permission;
  if (permission !== "granted" && askPermission) {
    permission = await Notification.requestPermission();
  }
  if (permission !== "granted") {
    throw new Error("Benachrichtigungen wurden im Browser nicht erlaubt.");
  }

  setStatus("Wird aktiviert...");

  const registration = await navigator.serviceWorker.register("./firebase-messaging-sw.js", {
    scope: "./"
  });
  await navigator.serviceWorker.ready;

  const token = await getToken(messaging, {
    vapidKey: FIREBASE_WEB_PUSH_PUBLIC_KEY,
    serviceWorkerRegistration: registration
  });

  if (!token) throw new Error("Kein FCM-Registrierungstoken erhalten.");

  await registerPushDevice({
    token,
    label: `${navigator.platform || "Browser"} | ${navigator.userAgent.slice(0, 90)}`
  });

  localStorage.setItem(TOKEN_KEY, token);
  localStorage.setItem(ENABLED_KEY, "true");
  toggle.checked = true;
  setStatus("Ein");
}

async function disableBackgroundPush() {
  setStatus("Wird deaktiviert...");

  const savedToken = localStorage.getItem(TOKEN_KEY);
  if (savedToken) {
    try {
      await unregisterPushDevice({ token: savedToken });
    } catch (error) {
      console.warn("Push-Token konnte serverseitig nicht entfernt werden:", error);
    }
  }

  if (messaging) {
    try {
      await deleteToken(messaging);
    } catch (error) {
      console.warn("Lokaler FCM-Token konnte nicht geloescht werden:", error);
    }
  }

  localStorage.removeItem(TOKEN_KEY);
  localStorage.setItem(ENABLED_KEY, "false");
  toggle.checked = false;
  setStatus("Aus");
}

function setStatus(text) {
  if (statusEl) statusEl.textContent = text;
}
