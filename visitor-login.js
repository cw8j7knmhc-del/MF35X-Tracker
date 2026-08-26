/* MF35X Besucher-Passwortschutz V9.6.0 – Passwort selbst änderbar + Web Push */

// =============================================================
// BESUCHERPASSWORT – NUR DEN TEXT ZWISCHEN DEN ANFÜHRUNGSZEICHEN ÄNDERN
// =============================================================
const VISITOR_PASSWORD = "mf35x";

const SESSION_KEY = "mf35x_visitor_access_v1";
const LEAFLET_SCRIPT_URL = "https://unpkg.com/leaflet/dist/leaflet.js";
const TRACKER_SCRIPT_URL = "./script.js?v=9.5.6";
const PUSH_CLIENT_URL = "./push-client.js?v=9.6.0";

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

logoutButton.addEventListener("click", async () => {
  try {
    if (typeof window.MF35XPushDisable === "function") {
      await window.MF35XPushDisable();
    }
  } catch (error) {
    console.warn("Push konnte beim Abmelden nicht vollständig entfernt werden:", error);
  }

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

    // Die Live-Daten bleiben wie bisher hinter dem Besucherpasswort.
    await import(`${TRACKER_SCRIPT_URL}-${Date.now()}`);
    trackerStarted = true;

    // Web Push wird erst NACH erfolgreichem Besucher-Login initialisiert.
    // Ein Fehler in der Push-Schicht darf die funktionierende Trackeranzeige
    // niemals blockieren.
    import(`${PUSH_CLIENT_URL}-${Date.now()}`).catch(error => {
      console.warn("Web Push konnte nicht initialisiert werden:", error);
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
