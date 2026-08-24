/* MF35X Besucher-Passwortschutz V9.5.8 – Passwort selbst änderbar */

// =============================================================
// BESUCHERPASSWORT – NUR DEN TEXT ZWISCHEN DEN ANFÜHRUNGSZEICHEN ÄNDERN
// =============================================================
const VISITOR_PASSWORD = "test";

const SESSION_KEY = "mf35x_visitor_access_v1";
const LEAFLET_SCRIPT_URL = "https://unpkg.com/leaflet/dist/leaflet.js";
const TRACKER_SCRIPT_URL = "./script.js?v=9.5.6";

const loginSection = document.getElementById("visitorLogin");
const loginForm = document.getElementById("visitorLoginForm");
const passwordInput = document.getElementById("visitorPassword");
const loginButton = document.getElementById("visitorLoginButton");
const loginError = document.getElementById("visitorLoginError");
const visitorApp = document.getElementById("visitorApp");
const logoutButton = document.getElementById("visitorLogout");

let trackerStarted = false;

loginForm.addEventListener("submit", async event => {
  event.preventDefault();
  loginError.textContent = "";
  loginButton.disabled = true;
  loginButton.textContent = "Passwort wird geprüft...";

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
    if (!trackerStarted) {
      loginButton.disabled = false;
      loginButton.textContent = "Besucheransicht öffnen";
    }
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

async function openVisitorApp() {
  if (trackerStarted) return;

  loginButton.disabled = true;
  loginButton.textContent = "Live-Daten werden geladen...";
  loginSection.hidden = true;
  visitorApp.hidden = false;

  try {
    await loadLeaflet();

    // script.js initialisiert Firebase. Der Import erfolgt deshalb bewusst
    // erst nach erfolgreicher Passwortprüfung.
    await import(`${TRACKER_SCRIPT_URL}-${Date.now()}`);
    trackerStarted = true;
  } catch (error) {
    visitorApp.hidden = true;
    loginSection.hidden = false;
    throw error;
  }
}

function loadLeaflet() {
  if (window.L) return Promise.resolve();

  return new Promise((resolve, reject) => {
    const script = document.createElement("script");
    script.src = LEAFLET_SCRIPT_URL;
    script.async = true;
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
  loginButton.textContent = "Besucheransicht öffnen";
  passwordInput.value = "";
  passwordInput.focus();
}
