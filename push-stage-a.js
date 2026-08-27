/* MF35X Web Push – Stufe A
 * Isolierter Empfangstest für echte Hintergrund-Pushnachrichten.
 * Keine Realtime-Database-Schreibzugriffe. Keine Tracker-/ESP32-Steuerung.
 */

import { getApps, getApp, initializeApp } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js";
import { getMessaging, getToken, deleteToken, isSupported, onMessage } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-messaging.js";
import { firebaseConfig } from "./firebase-config.js";
import { MF35X_VAPID_PUBLIC_KEY } from "./push-config.js";

const adminContent = document.getElementById("adminContent");
if (adminContent && !document.getElementById("webPushStageASettings")) {
  initStageA();
}

function initStageA() {
  const section = document.createElement("section");
  section.id = "webPushStageASettings";
  section.className = "settings admin-settings";
  section.innerHTML = `
    <h2>Echter Push-Test · Stufe A</h2>
    <p class="settings-note settings-note-block">
      Dieser Test ist vollständig von ESP32, Rennaufzeichnung und Live-Tracker getrennt.
      Es wird nur ein Firebase-Web-Push-Empfänger eingerichtet. Der Service Worker besitzt
      keinen Cache- und keinen Fetch-Handler und kann daher keine Tracker-Requests abfangen.
    </p>
    <div class="settings-actions">
      <button id="webPushStageAPrepare" class="save-button">Push-Test vorbereiten</button>
      <button id="webPushStageACopy" class="reset-button" disabled>Test-Token kopieren</button>
      <button id="webPushStageADisable" class="reset-button" disabled>Push-Test deaktivieren</button>
      <span id="webPushStageAStatus" class="config-status">Noch nicht eingerichtet.</span>
    </div>
    <p id="webPushStageATokenHint" class="settings-note settings-note-block">
      Es werden keine Push-Tokens in Firebase RTDB oder im Repository gespeichert.
    </p>
  `;

  const firstSettings = adminContent.querySelector(".settings.admin-settings");
  if (firstSettings) firstSettings.insertAdjacentElement("afterend", section);
  else adminContent.prepend(section);

  const prepareButton = document.getElementById("webPushStageAPrepare");
  const copyButton = document.getElementById("webPushStageACopy");
  const disableButton = document.getElementById("webPushStageADisable");
  const status = document.getElementById("webPushStageAStatus");
  const tokenHint = document.getElementById("webPushStageATokenHint");

  let currentToken = "";
  let messaging = null;
  let foregroundListenerInstalled = false;

  prepareButton.addEventListener("click", async () => {
    prepareButton.disabled = true;
    setStatus("Prüfe Browser und Service Worker …", "pending");

    try {
      if (!window.isSecureContext) {
        throw new Error("Push benötigt HTTPS.");
      }
      if (!("serviceWorker" in navigator) || !("PushManager" in window) || !("Notification" in window)) {
        throw new Error("Dieser Browser unterstützt Web Push nicht vollständig.");
      }

      const supported = await isSupported();
      if (!supported) {
        throw new Error("Firebase Messaging wird auf diesem Browser nicht unterstützt.");
      }

      if (isIosLike() && !isStandaloneHomeScreenApp()) {
        throw new Error("Auf iPhone/iPad muss der Tracker zuerst als Home-Screen-App geöffnet werden.");
      }

      let permission = Notification.permission;
      if (permission === "default") {
        permission = await Notification.requestPermission();
      }
      if (permission !== "granted") {
        throw new Error("Benachrichtigungen wurden nicht erlaubt.");
      }

      const registration = await navigator.serviceWorker.register(
        "./firebase-messaging-sw.js",
        { scope: "./", updateViaCache: "none" }
      );
      await registration.update();
      await navigator.serviceWorker.ready;

      const app = getApps().length ? getApp() : initializeApp(firebaseConfig);
      messaging = getMessaging(app);

      currentToken = await getToken(messaging, {
        vapidKey: MF35X_VAPID_PUBLIC_KEY,
        serviceWorkerRegistration: registration
      });

      if (!currentToken) {
        throw new Error("Firebase hat keinen Push-Token geliefert.");
      }

      if (!foregroundListenerInstalled) {
        foregroundListenerInstalled = true;
        onMessage(messaging, payload => {
          const title = payload?.notification?.title || payload?.data?.title || "MF35X Tracker";
          setStatus(`Testnachricht im Vordergrund empfangen: ${title}`, "success");
        });
      }

      copyButton.disabled = false;
      disableButton.disabled = false;
      tokenHint.textContent = `Push-Empfänger bereit · Token ${shortToken(currentToken)} · vollständiger Token nur über „Test-Token kopieren“.`;
      setStatus("Bereit für eine Firebase-Testnachricht.", "success");
    } catch (error) {
      console.error("Web-Push Stufe A konnte nicht vorbereitet werden:", error);
      currentToken = "";
      copyButton.disabled = true;
      disableButton.disabled = true;
      tokenHint.textContent = "Es wurde nichts an ESP32, Rennaufzeichnung oder Live-Tracker geändert.";
      setStatus(error?.message || "Push-Einrichtung fehlgeschlagen.", "error");
    } finally {
      prepareButton.disabled = false;
    }
  });

  copyButton.addEventListener("click", async () => {
    if (!currentToken) return;
    try {
      await navigator.clipboard.writeText(currentToken);
      setStatus("Test-Token kopiert. Bereit für Firebase „Testnachricht senden“.", "success");
    } catch (error) {
      setStatus("Token konnte nicht in die Zwischenablage kopiert werden.", "error");
    }
  });

  disableButton.addEventListener("click", async () => {
    disableButton.disabled = true;
    try {
      if (messaging) {
        await deleteToken(messaging);
      }
      currentToken = "";
      copyButton.disabled = true;
      tokenHint.textContent = "Push-Test deaktiviert. Der passive Service Worker darf installiert bleiben; er besitzt keinen Fetch-/Cache-Handler.";
      setStatus("Push-Test deaktiviert.", "success");
    } catch (error) {
      setStatus("Push-Token konnte nicht gelöscht werden: " + (error?.message || error), "error");
    } finally {
      disableButton.disabled = false;
    }
  });

  function setStatus(text, state = "") {
    status.textContent = text;
    status.className = "config-status";
    if (state) status.classList.add(`config-status-${state}`);
  }
}

function isIosLike() {
  return /iPhone|iPad|iPod/i.test(navigator.userAgent) ||
    (navigator.platform === "MacIntel" && navigator.maxTouchPoints > 1);
}

function isStandaloneHomeScreenApp() {
  return window.matchMedia?.("(display-mode: standalone)")?.matches === true ||
    navigator.standalone === true;
}

function shortToken(token) {
  if (!token || token.length < 24) return "vorhanden";
  return `${token.slice(0, 10)}…${token.slice(-8)}`;
}
