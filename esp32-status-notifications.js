/* MF35X Tracker – kostenlose ESP32 Online/Offline Browser-Benachrichtigung · nur Admin */
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

const adminContent = document.getElementById("adminContent");

// Diese Funktion darf ausschliesslich auf der Admin-Seite laufen.
if (!adminContent) {
  throw new Error("ESP32-Statusbenachrichtigung ist nur fuer den Admin-Bereich vorgesehen.");
}

const app = getApps().length ? getApp() : initializeApp(firebaseConfig);
const db = getDatabase(app);

setupAdminNotificationSection();

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
  // So entsteht beim Oeffnen/Neuladen der Admin-Seite keine Online-/Offline-Meldung.
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

function setupAdminNotificationSection() {
  if (document.getElementById("esp32NotificationSettings")) return;

  const section = document.createElement("section");
  section.id = "esp32NotificationSettings";
  section.className = "settings admin-settings";
  section.innerHTML = `
    <h2>Benachrichtigungen</h2>
    <p class="settings-note settings-note-block">
      Diese Einstellung ist nur im Admin-Bereich sichtbar. Bei aktiviertem Schalter meldet
      dieser Browser bzw. diese PWA einen echten Zustandswechsel des ESP32 auf Offline oder Online.
      Es werden keine Cloud Functions und kein kostenpflichtiger Firebase-Tarif verwendet.
    </p>
    <div class="settings-actions">
      <label class="switch-row">
        <span>ESP32 Online/Offline</span>
        <input id="esp32StatusNotificationToggle" type="checkbox">
        <span class="slider"></span>
      </label>
      <span id="esp32StatusNotifyStatus" class="config-status">Aus</span>
    </div>
  `;

  const firstSettings = adminContent.querySelector(".settings.admin-settings");
  if (firstSettings) {
    firstSettings.insertAdjacentElement("afterend", section);
  } else {
    adminContent.appendChild(section);
  }

  const toggle = document.getElementById("esp32StatusNotificationToggle");
  const status = document.getElementById("esp32StatusNotifyStatus");

  toggle.checked = localStorage.getItem(STORAGE_KEY) === "true";

  if (!("Notification" in window)) {
    toggle.checked = false;
    toggle.disabled = true;
    status.textContent = "Nicht unterstützt";
    return;
  }

  // Wurde die Browserfreigabe spaeter entzogen, darf der Schalter nicht weiter
  // scheinbar aktiv bleiben.
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
    if (toggle.checked) {
      status.textContent = "Ein – Zustandswechsel werden gemeldet";
    } else if (Notification.permission === "denied") {
      status.textContent = "Aus – Browser-Benachrichtigungen blockiert";
    } else {
      status.textContent = "Aus";
    }
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
