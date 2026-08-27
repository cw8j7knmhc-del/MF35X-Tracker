/* MF35X Web Push – Geräteverwaltung
 * Echte Hintergrund-Pushs über FCM/Cloudflare.
 * Keine ESP32-Steuerung, keine RTDB-Schreibzugriffe vom Browser.
 */

import { getApps, getApp, initializeApp } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js";
import { getMessaging, getToken, isSupported, onMessage } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-messaging.js";
import { firebaseConfig } from "./firebase-config.js";
import { MF35X_VAPID_PUBLIC_KEY, MF35X_PUSH_API_URL } from "./push-config.js?v=devices-20260827-1";

const ADMIN_CODE_SESSION_KEY = "mf35x_push_admin_code_v1";
const adminContent = document.getElementById("adminContent");

if (adminContent && !document.getElementById("webPushDeviceSettings")) {
  initPushDeviceAdmin();
}

function initPushDeviceAdmin() {
  const section = document.createElement("section");
  section.id = "webPushDeviceSettings";
  section.className = "settings admin-settings";
  section.innerHTML = `
    <h2>Push-Geräte & Benachrichtigungen</h2>
    <p class="settings-note settings-note-block">
      Echte Pushmeldungen funktionieren auch bei geschlossener Home-Screen-PWA und gesperrtem iPhone.
      Jedes registrierte Gerät kann einzeln komplett ein-/ausgeschaltet werden. Zusätzlich lassen sich
      die Meldungsarten pro Gerät getrennt aktivieren. ESP32, Rennaufzeichnung und GPIO11 werden dadurch nicht verändert.
    </p>

    <div class="settings-grid">
      <label>
        Push-Verwaltungscode
        <input id="pushAdminCode" type="password" autocomplete="off" placeholder="Cloudflare PUSH_ADMIN_CODE">
        <span class="field-hint">Wird nur für diese Sitzung im Browser gehalten und nie im Repository gespeichert.</span>
      </label>
      <label>
        Name dieses Geräts
        <input id="pushCurrentDeviceName" type="text" maxlength="40" placeholder="z. B. Dominic iPhone">
        <span class="field-hint">Name zur späteren Unterscheidung der registrierten Geräte.</span>
      </label>
    </div>

    <div class="settings-actions">
      <button id="pushRegisterCurrentDevice" class="save-button">Dieses Gerät hinzufügen / aktualisieren</button>
      <button id="pushLoadDevices" class="reset-button">Geräte laden</button>
      <span id="pushDeviceStatus" class="config-status">Noch nicht verbunden.</span>
    </div>

    <p class="settings-note settings-note-block">
      Kategorien: ESP32 Online/Offline, Batteriespannung, Öldruck, Öltemperatur und Zylinderkopftemperatur.
      Die alte lokale ESP32-Browserbenachrichtigung bleibt vorerst nur als Rückfallebene bestehen, bis diese neue Lösung vollständig getestet ist.
    </p>

    <div id="pushDeviceList" class="alarm-history">
      <div class="empty-history">Noch keine Geräte geladen.</div>
    </div>
  `;

  const firstSettings = adminContent.querySelector(".settings.admin-settings");
  if (firstSettings) firstSettings.insertAdjacentElement("afterend", section);
  else adminContent.prepend(section);

  const codeInput = document.getElementById("pushAdminCode");
  const nameInput = document.getElementById("pushCurrentDeviceName");
  const registerButton = document.getElementById("pushRegisterCurrentDevice");
  const loadButton = document.getElementById("pushLoadDevices");
  const status = document.getElementById("pushDeviceStatus");
  const list = document.getElementById("pushDeviceList");

  codeInput.value = readSessionCode();
  nameInput.value = defaultDeviceName();

  let messaging = null;
  let serviceWorkerRegistration = null;
  let foregroundListenerInstalled = false;

  registerButton.addEventListener("click", async () => {
    registerButton.disabled = true;
    setStatus("Push-Empfänger wird vorbereitet …", "pending");

    try {
      const code = requireAdminCode();
      const name = cleanDeviceName(nameInput.value);
      const token = await prepareCurrentPushToken();

      await apiPost("/api/devices/register", {
        code,
        name,
        token,
        platform: platformLabel()
      });

      rememberSessionCode(code);
      setStatus(`Gerät „${name}“ ist registriert.`, "success");
      await loadDevices();
    } catch (error) {
      console.error("Push-Gerät konnte nicht registriert werden:", error);
      setStatus(error?.message || "Gerät konnte nicht registriert werden.", "error");
    } finally {
      registerButton.disabled = false;
    }
  });

  loadButton.addEventListener("click", async () => {
    loadButton.disabled = true;
    try {
      await loadDevices();
    } catch (error) {
      console.error("Push-Geräte konnten nicht geladen werden:", error);
      setStatus(error?.message || "Geräte konnten nicht geladen werden.", "error");
    } finally {
      loadButton.disabled = false;
    }
  });

  codeInput.addEventListener("change", () => {
    const code = codeInput.value.trim();
    if (code) rememberSessionCode(code);
  });

  if (codeInput.value.trim()) {
    loadDevices().catch(() => {
      // Absichtlich still: ein alter/falscher Sitzungscode soll den Adminbereich nicht stören.
    });
  }

  async function prepareCurrentPushToken() {
    if (!window.isSecureContext) throw new Error("Push benötigt HTTPS.");
    if (!("serviceWorker" in navigator) || !("PushManager" in window) || !("Notification" in window)) {
      throw new Error("Dieser Browser unterstützt Web Push nicht vollständig.");
    }

    if (!(await isSupported())) {
      throw new Error("Firebase Messaging wird auf diesem Browser nicht unterstützt.");
    }

    if (isIosLike() && !isStandaloneHomeScreenApp()) {
      throw new Error("Auf iPhone/iPad muss der Tracker als Home-Screen-App geöffnet werden.");
    }

    let permission = Notification.permission;
    if (permission === "default") permission = await Notification.requestPermission();
    if (permission !== "granted") throw new Error("Benachrichtigungen wurden nicht erlaubt.");

    serviceWorkerRegistration = await navigator.serviceWorker.register(
      "./firebase-messaging-sw.js",
      { scope: "./", updateViaCache: "none" }
    );
    await serviceWorkerRegistration.update();
    await navigator.serviceWorker.ready;

    const app = getApps().length ? getApp() : initializeApp(firebaseConfig);
    messaging = getMessaging(app);

    const token = await getToken(messaging, {
      vapidKey: MF35X_VAPID_PUBLIC_KEY,
      serviceWorkerRegistration
    });

    if (!token) throw new Error("Firebase hat keinen Push-Token geliefert.");

    if (!foregroundListenerInstalled) {
      foregroundListenerInstalled = true;
      onMessage(messaging, async payload => {
        const title = payload?.notification?.title || payload?.data?.title || "MF35X Tracker";
        const body = payload?.notification?.body || payload?.data?.body || "Neue Tracker-Meldung";

        try {
          if (serviceWorkerRegistration) {
            await serviceWorkerRegistration.showNotification(title, {
              body,
              icon: "tractor.png",
              tag: payload?.data?.tag || "mf35x-push"
            });
          }
        } catch (error) {
          console.warn("Push im Vordergrund konnte nicht angezeigt werden:", error);
        }
      });
    }

    return token;
  }

  async function loadDevices() {
    const code = requireAdminCode();
    const result = await apiPost("/api/devices/list", { code });
    rememberSessionCode(code);
    renderDevices(Array.isArray(result.devices) ? result.devices : []);
    setStatus(`${result.devices?.length || 0} Gerät(e) geladen.`, "success");
  }

  function renderDevices(devices) {
    list.innerHTML = "";

    if (!devices.length) {
      const empty = document.createElement("div");
      empty.className = "empty-history";
      empty.textContent = "Noch kein Push-Gerät registriert.";
      list.appendChild(empty);
      return;
    }

    devices.forEach(device => list.appendChild(createDeviceCard(device)));
  }

  function createDeviceCard(device) {
    const card = document.createElement("div");
    card.className = "panel";
    card.style.marginBottom = "12px";

    const titleRow = document.createElement("div");
    titleRow.className = "panel-header";

    const title = document.createElement("strong");
    title.textContent = device.name || "Push-Gerät";
    titleRow.appendChild(title);

    const masterLabel = switchLabel("Gerät aktiv", !!device.enabled);
    titleRow.appendChild(masterLabel.label);
    card.appendChild(titleRow);

    const info = document.createElement("p");
    info.className = "settings-note settings-note-block";
    const lastSeen = device.updatedAt ? new Date(device.updatedAt).toLocaleString("de-AT") : "---";
    info.textContent = `${device.platform || "Gerät"} · Token ${device.tokenHint || "vorhanden"} · zuletzt aktualisiert ${lastSeen}`;
    card.appendChild(info);

    const prefs = device.notifications || {};
    const grid = document.createElement("div");
    grid.className = "settings-grid";

    const categoryDefs = [
      ["espStatus", "ESP32 Online/Offline"],
      ["battery", "Batteriespannung"],
      ["oilPressure", "Öldruck"],
      ["oilTemp", "Öltemperatur"],
      ["cylinderTemp", "Zylinderkopftemperatur"]
    ];

    const categoryInputs = {};
    for (const [key, labelText] of categoryDefs) {
      const item = switchLabel(labelText, prefs[key] !== false);
      categoryInputs[key] = item.input;
      grid.appendChild(item.label);
    }
    card.appendChild(grid);

    const actions = document.createElement("div");
    actions.className = "settings-actions";

    const testButton = document.createElement("button");
    testButton.className = "small-button";
    testButton.textContent = "Test senden";
    actions.appendChild(testButton);

    const deleteButton = document.createElement("button");
    deleteButton.className = "reset-button";
    deleteButton.textContent = "Gerät löschen";
    actions.appendChild(deleteButton);

    const deviceStatus = document.createElement("span");
    deviceStatus.className = "config-status";
    deviceStatus.textContent = device.enabled ? "Aktiv" : "Aus";
    actions.appendChild(deviceStatus);
    card.appendChild(actions);

    const saveState = async () => {
      deviceStatus.textContent = "Speichert …";
      try {
        await apiPost("/api/devices/update", {
          code: requireAdminCode(),
          id: device.id,
          enabled: masterLabel.input.checked,
          notifications: Object.fromEntries(
            Object.entries(categoryInputs).map(([key, input]) => [key, input.checked])
          )
        });
        deviceStatus.textContent = masterLabel.input.checked ? "Aktiv" : "Aus";
      } catch (error) {
        deviceStatus.textContent = "Fehler: " + (error?.message || error);
      }
    };

    masterLabel.input.addEventListener("change", saveState);
    Object.values(categoryInputs).forEach(input => input.addEventListener("change", saveState));

    testButton.addEventListener("click", async () => {
      testButton.disabled = true;
      deviceStatus.textContent = "Test wird gesendet …";
      try {
        await apiPost("/api/devices/test", {
          code: requireAdminCode(),
          id: device.id
        });
        deviceStatus.textContent = "Test gesendet";
      } catch (error) {
        deviceStatus.textContent = "Testfehler: " + (error?.message || error);
      } finally {
        testButton.disabled = false;
      }
    });

    deleteButton.addEventListener("click", async () => {
      if (!confirm(`Push-Gerät „${device.name || "Gerät"}“ wirklich löschen?`)) return;
      deleteButton.disabled = true;
      try {
        await apiPost("/api/devices/delete", {
          code: requireAdminCode(),
          id: device.id
        });
        await loadDevices();
      } catch (error) {
        deviceStatus.textContent = "Löschen fehlgeschlagen: " + (error?.message || error);
        deleteButton.disabled = false;
      }
    });

    return card;
  }

  function switchLabel(text, checked) {
    const label = document.createElement("label");
    label.className = "switch-row";

    const textSpan = document.createElement("span");
    textSpan.textContent = text;

    const input = document.createElement("input");
    input.type = "checkbox";
    input.checked = checked;

    const slider = document.createElement("span");
    slider.className = "slider";

    label.append(textSpan, input, slider);
    return { label, input };
  }

  function requireAdminCode() {
    const code = codeInput.value.trim();
    if (!code) throw new Error("Push-Verwaltungscode eingeben.");
    return code;
  }

  function setStatus(text, state = "") {
    status.textContent = text;
    status.className = "config-status";
    if (state) status.classList.add(`config-status-${state}`);
  }
}

async function apiPost(path, body) {
  const response = await fetch(`${MF35X_PUSH_API_URL}${path}`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(body)
  });

  let payload = null;
  try {
    payload = await response.json();
  } catch {
    payload = null;
  }

  if (!response.ok || payload?.ok === false) {
    throw new Error(payload?.error || `Push-API HTTP ${response.status}`);
  }

  return payload || { ok: true };
}

function readSessionCode() {
  try {
    return sessionStorage.getItem(ADMIN_CODE_SESSION_KEY) || "";
  } catch {
    return "";
  }
}

function rememberSessionCode(code) {
  try {
    sessionStorage.setItem(ADMIN_CODE_SESSION_KEY, code);
  } catch {
    // Kein Funktionsabbruch, wenn SessionStorage blockiert ist.
  }
}

function cleanDeviceName(value) {
  const text = String(value || "").trim().slice(0, 40);
  return text || defaultDeviceName();
}

function defaultDeviceName() {
  if (isIosLike()) return "iPhone";
  if (/Android/i.test(navigator.userAgent)) return "Android-Gerät";
  return "Dieses Gerät";
}

function platformLabel() {
  if (isIosLike()) return "iPhone/iPad PWA";
  if (/Android/i.test(navigator.userAgent)) return "Android";
  return navigator.platform || "Browser";
}

function isIosLike() {
  return /iPhone|iPad|iPod/i.test(navigator.userAgent) ||
    (navigator.platform === "MacIntel" && navigator.maxTouchPoints > 1);
}

function isStandaloneHomeScreenApp() {
  return window.matchMedia?.("(display-mode: standalone)")?.matches === true ||
    navigator.standalone === true;
}
