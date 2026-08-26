/* MF35X Tracker V9.6.0 – echte Web-Push-Registrierung (FCM/FID) */
import { initializeApp } from "https://www.gstatic.com/firebasejs/12.17.1/firebase-app.js";
import { getDatabase, ref, set, remove, serverTimestamp } from "https://www.gstatic.com/firebasejs/12.17.1/firebase-database.js";
import {
  getMessaging,
  isSupported,
  onRegistered,
  onUnregistered,
  register,
  unregister
} from "https://www.gstatic.com/firebasejs/12.17.1/firebase-messaging.js";
import { firebaseConfig } from "./firebase-config.js";
import { MF35X_VAPID_PUBLIC_KEY } from "./push-config.js";

const STORAGE_KEY = "mf35xNotificationsEnabled";
const PUSH_APP_NAME = "mf35x-push-v960";
const SW_URL = "./firebase-messaging-sw.js?v=9.6.0";
const SW_SCOPE = "./";
const PLACEHOLDER_PREFIX = "HIER_FIREBASE_";

const button = document.getElementById("requestNotifications");
const toggle = document.getElementById("notificationToggle");
const status = document.getElementById("notifyStatus");

let messaging = null;
let db = null;
let swRegistration = null;
let currentFid = null;
let callbacksInstalled = false;
let supported = false;

initPush().catch(error => {
  console.error("MF35X Push-Initialisierung fehlgeschlagen:", error);
  setStatus("Push nicht bereit");
});

async function initPush() {
  if (!button || !toggle || !status) return;

  supported =
    "serviceWorker" in navigator &&
    "Notification" in window &&
    await isSupported();

  if (!supported) {
    toggle.checked = false;
    toggle.disabled = true;
    button.disabled = true;
    setStatus("Nicht unterstützt");
    return;
  }

  if (!hasVapidKey()) {
    toggle.checked = false;
    toggle.disabled = true;
    button.disabled = true;
    setStatus("Push-Schlüssel fehlt");
    return;
  }

  const pushApp = initializeApp(firebaseConfig, PUSH_APP_NAME);
  db = getDatabase(pushApp);
  messaging = getMessaging(pushApp);

  installMessagingCallbacks();
  installUiHandlers();

  const wanted = localStorage.getItem(STORAGE_KEY) === "true";
  toggle.checked = wanted && Notification.permission === "granted";

  if (wanted && Notification.permission === "granted") {
    await ensureRegistration(false);
  } else {
    updateUi();
  }
}

function hasVapidKey() {
  return Boolean(
    MF35X_VAPID_PUBLIC_KEY &&
    !MF35X_VAPID_PUBLIC_KEY.startsWith(PLACEHOLDER_PREFIX) &&
    MF35X_VAPID_PUBLIC_KEY.length > 40
  );
}

function installMessagingCallbacks() {
  if (callbacksInstalled) return;
  callbacksInstalled = true;

  onRegistered(messaging, async fid => {
    currentFid = fid;
    await saveFid(fid);
    localStorage.setItem(STORAGE_KEY, "true");
    if (toggle) toggle.checked = true;
    updateUi("Ein · Push aktiv");
    console.log("MF35X Push registriert.");
  });

  onUnregistered(messaging, async fid => {
    if (fid) await deleteFid(fid);
    if (currentFid === fid) currentFid = null;
    console.log("MF35X Push deregistriert.");
  });
}

function installUiHandlers() {
  button.onclick = async () => {
    button.disabled = true;
    try {
      await enablePush();
    } catch (error) {
      console.error("MF35X Push konnte nicht aktiviert werden:", error);
      localStorage.setItem(STORAGE_KEY, "false");
      toggle.checked = false;
      setStatus("Aktivierung fehlgeschlagen");
    } finally {
      button.disabled = false;
      updateUi();
    }
  };

  toggle.onchange = async () => {
    toggle.disabled = true;
    try {
      if (toggle.checked) {
        await enablePush();
      } else {
        await disablePush();
      }
    } catch (error) {
      console.error("MF35X Push-Schalter fehlgeschlagen:", error);
      toggle.checked = localStorage.getItem(STORAGE_KEY) === "true";
      setStatus("Fehler");
    } finally {
      toggle.disabled = false;
      updateUi();
    }
  };

  window.MF35XPushDisable = disablePush;
}

async function enablePush() {
  if (!supported || !messaging) throw new Error("Push ist nicht initialisiert.");

  const permission = await Notification.requestPermission();
  if (permission !== "granted") {
    localStorage.setItem(STORAGE_KEY, "false");
    toggle.checked = false;
    setStatus(permission === "denied" ? "Im iPhone blockiert" : "Nicht erlaubt");
    return;
  }

  localStorage.setItem(STORAGE_KEY, "true");
  toggle.checked = true;
  await ensureRegistration(true);
}

async function ensureRegistration(userInitiated) {
  if (!swRegistration) {
    swRegistration = await navigator.serviceWorker.register(SW_URL, {
      scope: SW_SCOPE,
      updateViaCache: "none"
    });
  }

  setStatus(userInitiated ? "Push wird aktiviert…" : "Push wird geprüft…");

  await register(messaging, {
    vapidKey: MF35X_VAPID_PUBLIC_KEY,
    serviceWorkerRegistration: swRegistration
  });
}

async function disablePush() {
  localStorage.setItem(STORAGE_KEY, "false");
  if (toggle) toggle.checked = false;

  const oldFid = currentFid;
  currentFid = null;

  if (messaging) {
    try {
      await unregister(messaging);
    } catch (error) {
      console.warn("FCM-Deregistrierung fehlgeschlagen:", error);
    }
  }

  if (oldFid) {
    try {
      await deleteFid(oldFid);
    } catch (error) {
      console.warn("FID konnte in Firebase nicht entfernt werden:", error);
    }
  }

  updateUi("Aus");
}

async function saveFid(fid) {
  if (!db || !fid) return;
  await set(ref(db, `tracker/push/subscriptions/${fid}`), {
    fid,
    enabled: true,
    platform: "web",
    updatedAt: serverTimestamp()
  });
}

async function deleteFid(fid) {
  if (!db || !fid) return;
  await remove(ref(db, `tracker/push/subscriptions/${fid}`));
}

function updateUi(forcedText = null) {
  if (!button || !toggle || !status) return;

  const enabled =
    localStorage.getItem(STORAGE_KEY) === "true" &&
    Notification.permission === "granted";

  toggle.checked = enabled;
  button.style.display = Notification.permission === "granted" ? "none" : "inline-block";

  if (forcedText) {
    setStatus(forcedText);
  } else if (Notification.permission === "denied") {
    setStatus("Im iPhone blockiert");
  } else {
    setStatus(enabled ? "Ein · Push aktiv" : "Aus");
  }
}

function setStatus(text) {
  if (status) status.innerText = text;
}
