/* MF35X Tracker – kostenlose ESP32 Online/Offline Browser-Benachrichtigung */
import { getApps, getApp, initializeApp } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js";
import { getDatabase, ref, onValue } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-database.js";
import { firebaseConfig } from "./firebase-config.js";

const STORAGE_KEY = "mf35xEsp32StatusNotificationsEnabled";
const MIN_LIVE_TIMEOUT_MS = 5000;
const MAX_LIVE_TIMEOUT_MS = 15000;
const LIVE_TIMEOUT_FACTOR = 3;

let liveTimeoutMs = MIN_LIVE_TIMEOUT_MS;
let lastLiveTimestamp = 0;
let liveSnapshotSeen = false;
let stateInitialized = false;
let lastOnlineState = null;

const app = getApps().length ? getApp() : initializeApp(firebaseConfig);
const db = getDatabase(app);

setupEsp32NotificationSwitch();

onValue(ref(db, "tracker/device"), snapshot => {
  const device = snapshot.val() || {};
  const uploadInterval = numberValue(device.uploadIntervalMs);

  if (uploadInterval != null) {
    liveTimeoutMs = Math.max(
      MIN_LIVE_TIMEOUT_MS,
      Math.min(MAX_LIVE_TIMEOUT_MS, uploadInterval * LIVE_TIMEOUT_FACTOR)
    );
  }

  evaluateEsp32State();
});

onValue(ref(db, "tracker/live"), snapshot => {
  liveSnapshotSeen = true;
  const live = snapshot.val() || {};
  const timestamp = numberValue(live.timestamp);
  lastLiveTimestamp = timestamp != null ? timestamp : 0;
  evaluateEsp32State();
});

setInterval(evaluateEsp32State, 250);

function evaluateEsp32State() {
  // Erst nach dem ersten echten Firebase-Live-Snapshot einen Ausgangszustand festlegen.
  // So entsteht beim bloßen Öffnen/Neuladen der Seite keine Online-/Offline-Meldung.
  if (!liveSnapshotSeen) return;

  const online =
    lastLiveTimestamp > 0 &&
    Date.now() - lastLiveTimestamp <= liveTimeoutMs;

  if (!stateInitialized) {
    lastOnlineState = online;
    stateInitialized = true;
    return;
  }

  if (online === lastOnlineState) return;

  lastOnlineState = online;

  if (online) {
    notifyEsp32Status("ESP32 ist wieder online.");
  } else {
    notifyEsp32Status("ESP32 ist offline – es kommen keine neuen Tracker-Daten mehr an.");
  }
}

function setupEsp32NotificationSwitch() {
  const panel = document.querySelector(".notify-panel");
  if (!panel || document.getElementById("esp32StatusNotificationToggle")) return;

  const label = document.createElement("label");
  label.className = "switch-row";
  label.innerHTML = `
    <span>ESP32 Online/Offline</span>
    <input id="esp32StatusNotificationToggle" type="checkbox">
    <span class="slider"></span>
  `;

  const status = document.createElement("span");
  status.id = "esp32StatusNotifyStatus";
  status.textContent = "Aus";

  const adminLink = panel.querySelector(".admin-link");
  if (adminLink) {
    panel.insertBefore(label, adminLink);
    panel.insertBefore(status, adminLink);
  } else {
    panel.appendChild(label);
    panel.appendChild(status);
  }

  const toggle = label.querySelector("input");
  toggle.checked = localStorage.getItem(STORAGE_KEY) === "true";

  if (!("Notification" in window)) {
    toggle.checked = false;
    toggle.disabled = true;
    status.textContent = "Nicht unterstützt";
    return;
  }

  // Falls die Browserfreigabe entzogen wurde, kann der gespeicherte Schalter
  // nicht mehr aktiv bleiben.
  if (toggle.checked && Notification.permission !== "granted") {
    toggle.checked = false;
    localStorage.setItem(STORAGE_KEY, "false");
  }

  updateStatusText();

  toggle.addEventListener("change", async () => {
    if (toggle.checked && Notification.permission !== "granted") {
      const permission = await Notification.requestPermission();
      if (permission !== "granted") {
        toggle.checked = false;
      }
    }

    localStorage.setItem(STORAGE_KEY, toggle.checked ? "true" : "false");
    updateStatusText();
  });

  function updateStatusText() {
    status.textContent = toggle.checked ? "Ein" : "Aus";
  }
}

function notifyEsp32Status(body) {
  if (!("Notification" in window)) return;
  if (Notification.permission !== "granted") return;
  if (localStorage.getItem(STORAGE_KEY) !== "true") return;

  new Notification("MF35X Tracker", {
    body,
    icon: "tractor.png",
    tag: "mf35x-esp32-status"
  });
}

function numberValue(value) {
  if (value === undefined || value === null || value === "") return null;
  const number = Number(value);
  return Number.isFinite(number) ? number : null;
}
