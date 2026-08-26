/* MF35X Tracker – kostenlose ESP32 Online/Offline Browser-Benachrichtigung V9.5.15
 * Einstellung nur im Admin. Firebase-Statuslistener laufen nur, wenn die Funktion
 * auf genau diesem Browser/PWA aktiviert UND die Benachrichtigungsfreigabe erteilt ist.
 */
import { getApps, getApp, initializeApp } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js";
import { getDatabase, ref, onValue } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-database.js";
import { firebaseConfig } from "./firebase-config.js";

const STORAGE_KEY = "mf35xEsp32StatusNotificationsEnabled";
const MIN_LIVE_TIMEOUT_MS = 5000;
const MAX_LIVE_TIMEOUT_MS = 15000;
const LIVE_TIMEOUT_FACTOR = 3;

let db = null;
let liveTimeoutMs = MIN_LIVE_TIMEOUT_MS;
let lastLiveTimestamp = 0;
let liveSnapshotSeen = false;
let stateInitialized = false;
let lastOnlineState = null;
let monitoringActive = false;
let evaluationTimer = null;
let unsubscribeDevice = null;
let unsubscribeLive = null;

const adminContent = document.getElementById("adminContent");
const isAdminPage = !!adminContent;

// Nur im Admin-Bereich wird der Schalter angezeigt und bedienbar gemacht.
// In der Besucheransicht kann ausschließlich eine zuvor im Admin aktivierte
// Überwachung unsichtbar weiterlaufen.
if (isAdminPage) {
  setupAdminNotificationSection();
}

if (shouldMonitor()) {
  startMonitoring();
}

function getStatusDatabase() {
  if (db) return db;
  const app = getApps().length ? getApp() : initializeApp(firebaseConfig);
  db = getDatabase(app);
  return db;
}

function startMonitoring() {
  if (monitoringActive || !shouldMonitor()) return;

  const statusDb = getStatusDatabase();
  resetMonitoringState();
  monitoringActive = true;

  unsubscribeDevice = onValue(ref(statusDb, "tracker/device"), snapshot => {
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

  unsubscribeLive = onValue(ref(statusDb, "tracker/live"), snapshot => {
    liveSnapshotSeen = true;
    const live = snapshot.val() || {};
    const timestamp = numberValue(live.timestamp);
    lastLiveTimestamp = timestamp != null ? timestamp : 0;
    evaluateEsp32State();
  });

  evaluationTimer = setInterval(evaluateEsp32State, 250);
}

function stopMonitoring() {
  if (typeof unsubscribeDevice === "function") unsubscribeDevice();
  if (typeof unsubscribeLive === "function") unsubscribeLive();
  unsubscribeDevice = null;
  unsubscribeLive = null;

  if (evaluationTimer != null) {
    clearInterval(evaluationTimer);
    evaluationTimer = null;
  }

  monitoringActive = false;
  resetMonitoringState();
}

function resetMonitoringState() {
  liveTimeoutMs = MIN_LIVE_TIMEOUT_MS;
  lastLiveTimestamp = 0;
  liveSnapshotSeen = false;
  stateInitialized = false;
  lastOnlineState = null;
}

function shouldMonitor() {
  if (!("Notification" in window)) return false;
  if (Notification.permission !== "granted") return false;
  return storedEnabled();
}

function storedEnabled() {
  try {
    return localStorage.getItem(STORAGE_KEY) === "true";
  } catch (error) {
    return false;
  }
}

function storeEnabled(enabled) {
  try {
    localStorage.setItem(STORAGE_KEY, enabled ? "true" : "false");
  } catch (error) {
    console.warn("ESP32-Benachrichtigungseinstellung konnte nicht gespeichert werden:", error);
  }
}

function evaluateEsp32State() {
  if (!monitoringActive || !shouldMonitor()) return;

  // Erst nach dem ersten echten Firebase-Live-Snapshot einen Ausgangszustand festlegen.
  // So entsteht beim Öffnen/Neuladen von Admin- oder Besucheransicht keine Meldung.
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
  if (!adminContent || document.getElementById("esp32NotificationSettings")) return;

  const section = document.createElement("section");
  section.id = "esp32NotificationSettings";
  section.className = "settings admin-settings";
  section.innerHTML = `
    <h2>Benachrichtigungen</h2>
    <p class="settings-note settings-note-block">
      Diese Einstellung ist nur im Admin-Bereich sichtbar. Wenn du sie auf diesem Gerät einschaltest,
      bleibt die ESP32-Online/Offline-Überwachung auch dann aktiv, wenn du danach in die Besucheransicht wechselst.
      Andere Besucher sehen diesen Schalter nicht und können die Funktion nicht aktivieren.
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

  toggle.checked = storedEnabled();

  if (!("Notification" in window)) {
    toggle.checked = false;
    toggle.disabled = true;
    storeEnabled(false);
    stopMonitoring();
    status.textContent = "Nicht unterstützt";
    return;
  }

  if (toggle.checked && Notification.permission !== "granted") {
    toggle.checked = false;
    storeEnabled(false);
    stopMonitoring();
  }

  updateStatusText();

  toggle.addEventListener("change", async () => {
    if (toggle.checked && Notification.permission !== "granted") {
      const permission = await Notification.requestPermission();
      if (permission !== "granted") {
        toggle.checked = false;
      }
    }

    storeEnabled(toggle.checked);

    if (toggle.checked && Notification.permission === "granted") {
      startMonitoring();
    } else {
      stopMonitoring();
    }

    updateStatusText();
  });

  function updateStatusText() {
    if (toggle.checked && Notification.permission === "granted") {
      status.textContent = "Ein – auch in Besucheransicht aktiv";
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
  if (!storedEnabled()) return;

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
