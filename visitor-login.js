/* MF35X Besucher-Passwortschutz V9.5.10 – Login zuerst, optionale Statusueberwachung getrennt */

// =============================================================
// BESUCHERPASSWORT – NUR DEN TEXT ZWISCHEN DEN ANFUEHRUNGSZEICHEN AENDERN
// =============================================================
const VISITOR_PASSWORD = "mf35x";

const SESSION_KEY = "mf35x_visitor_access_v1";
const STATUS_NOTIFICATION_STORAGE_KEY = "mf35xEsp32StatusNotificationsEnabled";
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

/*
 * Besucher ohne Freigabe laden bewusst KEINE komplette Tracker-/Firebase-Logik.
 * Nur wenn die ESP32-Online/Offline-Benachrichtigung auf genau diesem Browser/PWA
 * vorher im Adminbereich aktiviert wurde, darf der kleine Statusmonitor bereits
 * vor dem Besucherlogin laufen. Dadurch bleibt die gewuenschte Admin-Funktion
 * erhalten, ohne normale Besucher mit der kompletten Liveansicht zu verbinden.
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

    rememberAccess();
    await openVisitorApp();
  } catch (error) {
    console.error("Besucher-Anmeldung fehlgeschlagen:", error);
    loginError.textContent = "Anmeldung fehlgeschlagen. Bitte Seite neu laden.";
  } finally {
    if (!visitorApp.hidden) return;
    loginButton.disabled = false;
    loginButton.textContent = "Besucheransicht oeffnen";
  }
});

logoutButton.addEventListener("click", () => {
  forgetAccess();
  location.reload();
});

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
    await loadLeaflet();
    await import(`${TRACKER_SCRIPT_URL}-${Date.now()}`);
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

function rememberAccess() {
  try {
    sessionStorage.setItem(SESSION_KEY, "granted");
  } catch (error) {
    console.warn("Besucherfreigabe konnte nicht gespeichert werden:", error);
  }
}

function hasRememberedAccess() {
  try {
    return sessionStorage.getItem(SESSION_KEY) === "granted";
  } catch (error) {
    return false;
  }
}

function forgetAccess() {
  try {
    sessionStorage.removeItem(SESSION_KEY);
  } catch (error) {
    console.warn("Besucherfreigabe konnte nicht entfernt werden:", error);
  }
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
