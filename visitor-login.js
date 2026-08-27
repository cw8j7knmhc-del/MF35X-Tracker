/* MF35X Besucher-Passwortschutz V9.5.12 – optional 1 Stunde angemeldet bleiben */

// =============================================================
// BESUCHERPASSWORT – NUR DEN TEXT ZWISCHEN DEN ANFUEHRUNGSZEICHEN AENDERN
// =============================================================
const VISITOR_PASSWORD = "mf35x";

const SESSION_KEY = "mf35x_visitor_access_v1";
const PERSISTENT_ACCESS_KEY = "mf35x_visitor_remember_v1";
const PERSISTENT_ACCESS_MS = 60 * 60 * 1000;
const STATUS_NOTIFICATION_STORAGE_KEY = "mf35xEsp32StatusNotificationsEnabled";
const LEGACY_VISITOR_NOTIFICATION_STORAGE_KEY = "mf35xNotificationsEnabled";
const LEAFLET_SCRIPT_URL = "https://unpkg.com/leaflet/dist/leaflet.js";
const TRACKER_SCRIPT_URL = "./script.js?v=9.5.6";
const ESP32_STATUS_NOTIFICATION_SCRIPT_URL = "./esp32-status-notifications.js?v=9.5.15";

const loginSection = document.getElementById("visitorLogin");
const loginForm = document.getElementById("visitorLoginForm");
const passwordInput = document.getElementById("visitorPassword");
const loginButton = document.getElementById("visitorLoginButton");
const loginError = document.getElementById("visitorLoginError");
const visitorApp = document.getElementById("visitorApp");
const logoutButton = document.getElementById("visitorLogout");

let trackerStarted = false;
let trackerStartPromise = null;
let statusMonitorStarted = false;
let statusMonitorStartPromise = null;
let persistentExpiryTimer = null;

const rememberLabel = document.createElement("label");
rememberLabel.className = "switch-row";
rememberLabel.style.justifyContent = "space-between";
rememberLabel.style.textAlign = "left";
rememberLabel.style.margin = "2px 0 4px";

const rememberText = document.createElement("span");
rememberText.textContent = "Auf diesem Gerät 1 Stunde angemeldet bleiben";

const rememberInput = document.createElement("input");
rememberInput.type = "checkbox";
rememberInput.id = "visitorRememberLogin";

const rememberSlider = document.createElement("span");
rememberSlider.className = "slider";
rememberLabel.append(rememberText, rememberInput, rememberSlider);
loginButton.insertAdjacentElement("beforebegin", rememberLabel);

/*
 * Besucher ohne Freigabe laden bewusst KEINE komplette Tracker-/Firebase-Logik.
 * Der alte Browser-Statusmonitor ist inzwischen stillgelegt; der Aufruf bleibt
 * nur als harmlose Kompatibilitaet zu bereits gespeicherten Browserwerten bestehen.
 */
startStatusMonitorIfEnabled().catch(error => {
  console.error("ESP32-Statusmonitor konnte nicht gestartet werden:", error);
});

loginForm.addEventListener("submit", async event => {
  event.preventDefault();
  loginError.textContent = "";
  loginButton.disabled = true;
  loginButton.textContent = "Passwort wird geprueft...";

  try {
    if (passwordInput.value !== VISITOR_PASSWORD) {
      loginError.textContent = "Falsches Besucherpasswort.";
      passwordInput.value = "";
      passwordInput.focus();
      return;
    }

    rememberSessionAccess();

    if (rememberInput.checked) {
      const expiresAt = Date.now() + PERSISTENT_ACCESS_MS;
      rememberPersistentAccess(expiresAt);
      schedulePersistentExpiry(expiresAt);
    } else {
      forgetPersistentAccess();
    }

    await openVisitorApp();
  } catch (error) {
    console.error("Besucher-Anmeldung fehlgeschlagen:", error);
    loginError.textContent = "Anmeldung fehlgeschlagen. Bitte Seite neu laden.";
  } finally {
    passwordInput.value = "";
    if (!visitorApp.hidden) return;
    loginButton.disabled = false;
    loginButton.textContent = "Besucheransicht oeffnen";
  }
});

logoutButton.addEventListener("click", () => {
  forgetAccess();
  location.reload();
});

const persistentExpiry = readPersistentAccessExpiry();
if (persistentExpiry > Date.now()) {
  rememberInput.checked = true;
  rememberSessionAccess();
  schedulePersistentExpiry(persistentExpiry);
} else if (persistentExpiry) {
  // Eine abgelaufene 1-Stunden-Freigabe beendet auch eine eventuell noch
  // vorhandene Session desselben Tabs/PWA-Fensters.
  forgetAccess();
}

if (hasRememberedAccess()) {
  openVisitorApp().catch(error => {
    console.error("Besucheransicht konnte nicht gestartet werden:", error);
    forgetAccess();
    showLoginError("Live-Daten konnten nicht geladen werden. Bitte erneut anmelden.");
  });
} else {
  passwordInput.focus();
}

async function startTrackerInBackground() {
  if (trackerStarted) return;
  if (trackerStartPromise) return trackerStartPromise;

  trackerStartPromise = (async () => {
    // Die alte Besucher-Alarmbenachrichtigung wird zentral abgeschaltet.
    // Dadurch kann script.js beim Laden weiterhin unveraendert initialisieren,
    // erzeugt aber auf der Besucheransicht keine lokalen Notification-Popups mehr.
    disableLegacyVisitorNotifications();

    await loadLeaflet();
    await import(`${TRACKER_SCRIPT_URL}-${Date.now()}`);

    // Die alten Besucher-Schalter werden erst NACH script.js entfernt, damit dessen
    // bestehende Initialisierung keine fehlenden DOM-Elemente vorfindet.
    removeLegacyVisitorNotificationControls();

    await startStatusMonitorIfEnabled();
    trackerStarted = true;
  })();

  try {
    await trackerStartPromise;
  } finally {
    trackerStartPromise = null;
  }
}

async function startStatusMonitorIfEnabled() {
  if (!shouldRunStatusMonitor()) return;
  if (statusMonitorStarted) return;
  if (statusMonitorStartPromise) return statusMonitorStartPromise;

  statusMonitorStartPromise = import(ESP32_STATUS_NOTIFICATION_SCRIPT_URL)
    .then(() => {
      statusMonitorStarted = true;
    });

  try {
    await statusMonitorStartPromise;
  } finally {
    statusMonitorStartPromise = null;
  }
}

function shouldRunStatusMonitor() {
  try {
    return (
      "Notification" in window &&
      Notification.permission === "granted" &&
      localStorage.getItem(STATUS_NOTIFICATION_STORAGE_KEY) === "true"
    );
  } catch (error) {
    return false;
  }
}

function disableLegacyVisitorNotifications() {
  try {
    localStorage.setItem(LEGACY_VISITOR_NOTIFICATION_STORAGE_KEY, "false");
  } catch (error) {
    console.warn("Alte Besucher-Benachrichtigung konnte nicht deaktiviert werden:", error);
  }
}

function removeLegacyVisitorNotificationControls() {
  const panel = document.querySelector(".notify-panel");
  if (!panel) return;

  panel.querySelector("#requestNotifications")?.remove();
  panel.querySelector("#notificationToggle")?.closest("label")?.remove();
  panel.querySelector("#notifyStatus")?.remove();

  // Der Admin-Link bleibt bewusst erhalten.
  if (!panel.querySelector(".admin-link")) {
    panel.remove();
  }
}

async function openVisitorApp() {
  loginButton.disabled = true;
  loginButton.textContent = "Live-Daten werden geladen...";

  try {
    await startTrackerInBackground();

    loginSection.hidden = true;
    visitorApp.hidden = false;

    // Leaflet wird erst nach erfolgreicher Freigabe geladen. Ein Resize nach dem
    // Einblenden sorgt trotzdem dafuer, dass die Karte korrekt zeichnet.
    requestAnimationFrame(() => {
      window.dispatchEvent(new Event("resize"));
      setTimeout(() => window.dispatchEvent(new Event("resize")), 250);
    });
  } catch (error) {
    visitorApp.hidden = true;
    loginSection.hidden = false;
    throw error;
  }
}

function loadLeaflet() {
  if (window.L) return Promise.resolve();

  return new Promise((resolve, reject) => {
    const existing = document.querySelector('script[data-mf35x-leaflet="1"]');
    if (existing) {
      existing.addEventListener("load", resolve, { once: true });
      existing.addEventListener(
        "error",
        () => reject(new Error("Leaflet konnte nicht geladen werden.")),
        { once: true }
      );
      return;
    }

    const script = document.createElement("script");
    script.src = LEAFLET_SCRIPT_URL;
    script.async = true;
    script.dataset.mf35xLeaflet = "1";
    script.onload = resolve;
    script.onerror = () => reject(new Error("Leaflet konnte nicht geladen werden."));
    document.head.appendChild(script);
  });
}

function rememberSessionAccess() {
  try {
    sessionStorage.setItem(SESSION_KEY, "granted");
  } catch (error) {
    console.warn("Besucherfreigabe konnte nicht gespeichert werden:", error);
  }
}

function rememberPersistentAccess(expiresAt) {
  try {
    localStorage.setItem(PERSISTENT_ACCESS_KEY, JSON.stringify({ expiresAt }));
  } catch (error) {
    console.warn("Besucherfreigabe konnte nicht dauerhaft gespeichert werden:", error);
  }
}

function readPersistentAccessExpiry() {
  try {
    const raw = localStorage.getItem(PERSISTENT_ACCESS_KEY);
    if (!raw) return 0;
    const parsed = JSON.parse(raw);
    const expiresAt = Number(parsed?.expiresAt || 0);
    return Number.isFinite(expiresAt) ? expiresAt : 0;
  } catch {
    return 0;
  }
}

function hasRememberedAccess() {
  try {
    if (sessionStorage.getItem(SESSION_KEY) === "granted") return true;
    const expiresAt = readPersistentAccessExpiry();
    return expiresAt > Date.now();
  } catch (error) {
    return false;
  }
}

function schedulePersistentExpiry(expiresAt) {
  if (persistentExpiryTimer) clearTimeout(persistentExpiryTimer);
  const remaining = Math.max(0, expiresAt - Date.now());
  persistentExpiryTimer = setTimeout(() => {
    forgetAccess();
    location.reload();
  }, Math.min(remaining, 2147483647));
}

function forgetPersistentAccess() {
  if (persistentExpiryTimer) clearTimeout(persistentExpiryTimer);
  persistentExpiryTimer = null;
  try {
    localStorage.removeItem(PERSISTENT_ACCESS_KEY);
  } catch (error) {
    console.warn("Dauerhafte Besucherfreigabe konnte nicht entfernt werden:", error);
  }
}

function forgetAccess() {
  try {
    sessionStorage.removeItem(SESSION_KEY);
  } catch (error) {
    console.warn("Besucherfreigabe konnte nicht entfernt werden:", error);
  }
  forgetPersistentAccess();
}

function showLoginError(message) {
  visitorApp.hidden = true;
  loginSection.hidden = false;
  loginError.textContent = message;
  loginButton.disabled = false;
  loginButton.textContent = "Besucheransicht oeffnen";
  passwordInput.value = "";
  passwordInput.focus();
}
