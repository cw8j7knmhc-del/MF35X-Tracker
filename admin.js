/* MF35X Tracker Admin V9.5.14 */

import { initializeApp } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js";
import { getDatabase, ref, onValue, set, get, update, query, orderByChild, startAt, endAt } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-database.js";
import { firebaseConfig } from "./firebase-config.js";

const ADMIN_PASSWORD = "WaGramHaZza35X!";
const OTA_MANIFEST_URL =
  "https://raw.githubusercontent.com/cw8j7knmhc-del/MF35X-Tracker/main/firmware/manifest.json";

const DEFAULT_LIMITS = {
  batteryWarn: 12.2,
  batteryAlarm: 11.8,
  oilPressureWarn: 2.0,
  oilPressureAlarm: 1.2,
  oilTempWarn: 110,
  oilTempAlarm: 125,
  cylTempWarn: 180,
  cylTempAlarm: 220
};

const DEFAULT_OUTPUT_CONFIG = {
  speed_enable_kmh: 60,
  rpm_on: 3200,
  rpm_off: 3150
};

const DEFAULT_INTERVALS = {
  rpm_firebase_update_ms: 250,
  oil_pressure_update_ms: 100,
  temperature_update_ms: 1000,
  gps_update_ms: 1000,
  history_update_ms: 5000
};

const INTERVAL_RULES = {
  rpm_firebase_update_ms: {
    id: "setRpmFirebaseUpdateMs",
    min: 100,
    max: 5000
  },
  oil_pressure_update_ms: {
    id: "setOilPressureUpdateMs",
    min: 50,
    max: 5000
  },
  temperature_update_ms: {
    id: "setTemperatureUpdateMs",
    min: 250,
    max: 10000
  },
  gps_update_ms: {
    id: "setGpsUpdateMs",
    min: 100,
    max: 3000
  },
  history_update_ms: {
    id: "setHistoryUpdateMs",
    min: 1000,
    max: 60000
  }
};

const app = initializeApp(firebaseConfig);
const db = getDatabase(app);

let adminStarted = false;
let historySupported = false;
let recordingState = { enabled: false };
let systemCommandsSupported = false;
let gpsSoftwareRestartSupported = false;
let otaSupported = false;
let otaPartitionReady = false;
let currentFirmwareVersionCode = 0;
let latestOtaManifest = null;
let pendingSystemCommand = null;
let offlineHistoryPendingCount = 0;
let maxValuesDeviceOwned = false;
let alarmHistoryDeviceOwned = false;


// ==================================================
// RENNAUSWERTUNG - NUR NACH ADMIN-LOGIN INITIALISIERT
// ==================================================
const ANALYSIS_METRICS = {
  cylinder_temp: { label: "Zylinderkopf", unit: "°C", color: "#ff4040", group: "temperature", decimals: 0 },
  oil_temp: { label: "Motoröl", unit: "°C", color: "#ff951f", group: "temperature", decimals: 0 },
  gear_oil_temp: { label: "Getriebeöl", unit: "°C", color: "#ffd24b", group: "temperature", decimals: 0 },
  oil_pressure: { label: "Öldruck", unit: "bar", color: "#2e9bff", group: "operating", decimals: 1, axis: "yPressure" },
  rpm: { label: "Drehzahl", unit: "U/min", color: "#a04cff", group: "operating", decimals: 0, axis: "yRpm" },
  speed_kmh: { label: "Geschwindigkeit", unit: "km/h", color: "#43ff5f", group: "operating", decimals: 1, axis: "ySpeed" }
};

let analysisRaces = {};
let analysisCurrentSamples = [];
let analysisCurrentRaceId = "";
let analysisSettings = {};
let analysisTemperatureChart = null;
let analysisOperatingChart = null;
let analysisInitialized = false;
let analysisRaceSelect = null;
let analysisFromTime = null;
let analysisToTime = null;
let analysisLoadRangeButton = null;
let analysisLoadFullButton = null;
let analysisExportButton = null;
let analysisDeleteButton = null;

document.getElementById("loginButton").addEventListener("click", login);

document.getElementById("adminPassword").addEventListener("keydown", event => {
  if (event.key === "Enter") {
    login();
  }
});

function login() {
  const enteredPassword = document.getElementById("adminPassword").value;

  if (enteredPassword === ADMIN_PASSWORD) {
    document.getElementById("loginBox").classList.add("hidden");
    document.getElementById("adminContent").classList.remove("hidden");

    if (!adminStarted) {
      adminStarted = true;
      initAdmin();
    }

    if (location.hash === "#raceAnalysis") {
      setTimeout(() => document.getElementById("raceAnalysis")?.scrollIntoView({ behavior: "smooth", block: "start" }), 50);
    }
  } else {
    alert("Falsches Passwort.");
  }
}

function initAdmin() {
  listenSettings();
  listenOutputConfig();
  listenIntervals();
  listenMaxValues();
  listenAlarmHistory();
  listenHistorySupport();
  listenRecordingState();
  listenSystemSupport();
  listenSystemCommands();
  initRaceAnalysis();

  document.getElementById("restartEsp32").addEventListener("click", () => {
    sendSystemCommand(
      "esp32_reboot",
      "ESP32-Neustart",
      "ESP32 wirklich neu starten? Die Live-Daten sind für einige Sekunden nicht verfügbar."
    );
  });

  document.getElementById("restartWifi").addEventListener("click", () => {
    sendSystemCommand(
      "wifi_restart",
      "WLAN-Neustart",
      "WLAN wirklich neu starten? Der Tracker trennt kurz die WLAN-Verbindung und verbindet sich danach wieder."
    );
  });

  document.getElementById("restartGps").addEventListener("click", () => {
    sendSystemCommand(
      "gps_restart",
      "GPS-Neustart",
      "GPS wirklich softwareseitig neu starten? Die GPS-Position kann danach kurz ausfallen."
    );
  });

  document.getElementById("checkFirmwareUpdate").addEventListener("click", checkFirmwareUpdate);
  document.getElementById("installFirmwareUpdate").addEventListener("click", installFirmwareUpdate);

  document.getElementById("saveSettings").addEventListener("click", saveSettings);

  document.getElementById("resetSettings").addEventListener("click", async () => {
    await set(ref(db, "tracker/settings"), DEFAULT_LIMITS);
    alert("Standardwerte geladen.");
  });

  document.getElementById("saveOutputConfig").addEventListener("click", saveOutputConfig);

  document.getElementById("resetOutputConfig").addEventListener("click", async () => {
    try {
      await set(ref(db, "tracker/config/external_output"), DEFAULT_OUTPUT_CONFIG);
      setOutputConfigStatus("Standardwerte gespeichert.", "success");
    } catch (error) {
      setOutputConfigStatus("Speichern fehlgeschlagen: " + error.message, "error");
    }
  });

  document.getElementById("saveIntervals").addEventListener("click", saveIntervals);

  document.getElementById("resetIntervals").addEventListener("click", async () => {
    try {
      await set(ref(db, "tracker/config/intervals"), DEFAULT_INTERVALS);
      setIntervalStatus("Standardintervalle gespeichert.", "success");
    } catch (error) {
      setIntervalStatus("Speichern fehlgeschlagen: " + error.message, "error");
    }
  });

  document.getElementById("startRecording").addEventListener("click", startRecording);
  document.getElementById("stopRecording").addEventListener("click", stopRecording);

  document.getElementById("resetMaxValues").addEventListener("click", resetMaxValues);

  document.getElementById("clearAlarmHistory").addEventListener("click", clearAlarmHistory);
}

function listenSystemSupport() {
  onValue(ref(db, "tracker/device"), snapshot => {
    const device = snapshot.val() || {};

    systemCommandsSupported = device.systemCommandsSupported === true;
    gpsSoftwareRestartSupported = device.gpsSoftwareRestartSupported === true;
    otaSupported = device.otaSupported === true && device.otaSignedUpdates === true;
    otaPartitionReady = device.otaPartitionReady === true;
    currentFirmwareVersionCode = Number(device.firmwareVersionCode || 0);
    offlineHistoryPendingCount = Number(device.historyOfflinePending || 0);
    maxValuesDeviceOwned = device.maxValuesDeviceOwned === true;
    alarmHistoryDeviceOwned = device.alarmHistoryDeviceOwned === true;

    const offlineBadge = document.getElementById("offlineBufferStatus");
    const offlineDetail = document.getElementById("offlineBufferDetail");
    if (offlineBadge) {
      const supported = device.historyOfflineBufferSupported === true;
      const ready = device.historyOfflineBufferReady === true;
      const full = device.historyOfflineBufferFull === true;
      const pending = offlineHistoryPendingCount;

      if (!supported) {
        offlineBadge.textContent = "nicht unterstützt";
        offlineBadge.className = "recording-badge recording-badge-wait";
      } else if (!ready) {
        offlineBadge.textContent = "FEHLER";
        offlineBadge.className = "recording-badge recording-badge-wait";
      } else if (full) {
        offlineBadge.textContent = `VOLL · ${pending} offen`;
        offlineBadge.className = "recording-badge recording-badge-wait";
      } else if (pending > 0) {
        offlineBadge.textContent = `${pending} gepuffert`;
        offlineBadge.className = "recording-badge recording-badge-on";
      } else {
        offlineBadge.textContent = "bereit · 0";
        offlineBadge.className = "recording-badge recording-badge-on";
      }

      if (offlineDetail) {
        const total = Number(device.historyOfflineFsTotalBytes || 0);
        const used = Number(device.historyOfflineFsUsedBytes || 0);
        const replayed = Number(device.historyOfflineReplayed || 0);
        const dropped = Number(device.historyOfflineDropped || 0);
        const psram = Number(device.historyOfflinePsramBytes || 0);
        const err = device.historyOfflineLastError ? ` · Fehler: ${device.historyOfflineLastError}` : "";
        offlineDetail.textContent =
          `Flash ${(used / 1024).toFixed(0)} / ${(total / 1024).toFixed(0)} KiB` +
          ` · PSRAM ${psram > 0 ? (psram / 1024 / 1024).toFixed(1) + " MiB" : "nicht aktiv"}` +
          ` · nachgesendet ${replayed} · verworfen ${dropped}${err}`;
      }
    }

    updateAnalysisDeleteButton();

    const firmware = device.firmware || "---";
    const firmwareBadge = document.getElementById("systemFirmwareStatus");
    firmwareBadge.textContent = firmware;
    firmwareBadge.className =
      "recording-badge " +
      (device.firmware ? "recording-badge-on" : "recording-badge-wait");

    const supportBadge = document.getElementById("systemSupportStatus");
    supportBadge.textContent = systemCommandsSupported ? "bereit" : "nicht unterstützt";
    supportBadge.className =
      "recording-badge " +
      (systemCommandsSupported ? "recording-badge-on" : "recording-badge-wait");

    const otaBadge = document.getElementById("otaSupportStatus");
    if (!otaSupported) {
      otaBadge.textContent = "Firmware ohne OTA";
      otaBadge.className = "recording-badge recording-badge-wait";
    } else if (!otaPartitionReady) {
      otaBadge.textContent = "OTA-Partition fehlt";
      otaBadge.className = "recording-badge recording-badge-wait";
    } else {
      otaBadge.textContent = device.otaAutomaticRollback === true
        ? "signiert + Rollback"
        : "signiert";
      otaBadge.className = "recording-badge recording-badge-on";
    }

    document.getElementById("restartEsp32").disabled = !systemCommandsSupported;
    document.getElementById("resetMaxValues").disabled = !systemCommandsSupported || !maxValuesDeviceOwned;
    document.getElementById("clearAlarmHistory").disabled = !systemCommandsSupported || !alarmHistoryDeviceOwned;
    document.getElementById("restartWifi").disabled = !systemCommandsSupported;
    document.getElementById("restartGps").disabled =
      !systemCommandsSupported || !gpsSoftwareRestartSupported;
    document.getElementById("checkFirmwareUpdate").disabled =
      !systemCommandsSupported || !otaSupported || !otaPartitionReady;

    updateInstallButton();
  });
}

async function checkFirmwareUpdate() {
  if (!otaSupported || !otaPartitionReady) {
    setOtaStatus("OTA ist auf der aktuell laufenden Firmware noch nicht bereit.", "error");
    return;
  }

  setOtaStatus("Prüfe GitHub auf eine neue signierte Firmware …", "pending");
  document.getElementById("installFirmwareUpdate").disabled = true;

  try {
    const response = await fetch(`${OTA_MANIFEST_URL}?t=${Date.now()}`, {
      cache: "no-store"
    });

    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }

    const manifest = await response.json();
    if (!manifest ||
        typeof manifest.version !== "string" ||
        !Number.isInteger(Number(manifest.versionCode)) ||
        manifest.signed !== true) {
      throw new Error("Manifest ist ungültig");
    }

    latestOtaManifest = manifest;
    const latestBadge = document.getElementById("otaLatestVersion");
    latestBadge.textContent = manifest.version;

    if (Number(manifest.versionCode) > currentFirmwareVersionCode) {
      latestBadge.className = "recording-badge recording-badge-on";
      setOtaStatus(`Update ${manifest.version} ist verfügbar.`, "success");
    } else {
      latestBadge.className = "recording-badge recording-badge-wait";
      setOtaStatus("Die installierte Firmware ist aktuell.", "success");
    }

    updateInstallButton();
  } catch (error) {
    latestOtaManifest = null;
    document.getElementById("otaLatestVersion").textContent = "Prüfung fehlgeschlagen";
    document.getElementById("otaLatestVersion").className =
      "recording-badge recording-badge-wait";
    setOtaStatus(`Updateprüfung fehlgeschlagen: ${error.message}`, "error");
  }
}

function updateInstallButton() {
  const button = document.getElementById("installFirmwareUpdate");
  if (!button) return;

  const newer = latestOtaManifest &&
    Number(latestOtaManifest.versionCode) > currentFirmwareVersionCode;

  button.disabled = !systemCommandsSupported || !otaSupported || !otaPartitionReady || !newer;
}

async function installFirmwareUpdate() {
  if (!latestOtaManifest ||
      Number(latestOtaManifest.versionCode) <= currentFirmwareVersionCode) {
    await checkFirmwareUpdate();
    return;
  }

  const target = latestOtaManifest.version;
  await sendSystemCommand(
    "ota_update",
    `Firmwareupdate auf ${target}`,
    `Firmware ${target} wirklich installieren? Der Tracker startet danach automatisch neu.`
  );
}

function setOtaStatus(text, state = "") {
  const element = document.getElementById("otaStatus");
  element.textContent = text;
  element.className = "config-status";
  if (state) element.classList.add(`config-status-${state}`);
}

function listenSystemCommands() {
  onValue(ref(db, "tracker/config/system_commands"), snapshot => {
    if (!pendingSystemCommand) return;

    const commands = snapshot.val() || {};
    const state = commands[pendingSystemCommand.command];
    if (!state || state.requestId !== pendingSystemCommand.requestId) return;

    const detail = state.message ? ` – ${state.message}` : "";

    if (state.status === "requested") {
      setSystemCommandStatus(
        `${pendingSystemCommand.label}: Befehl gesendet – wartet auf ESP32.`,
        "pending"
      );
    } else if (["checking", "downloading", "verifying", "restarting", "resetting", "clearing"].includes(state.status)) {
      setSystemCommandStatus(
        `${pendingSystemCommand.label}: ${state.status}${detail}`,
        "pending"
      );
      if (pendingSystemCommand.command === "ota_update") {
        setOtaStatus(`${pendingSystemCommand.label}: ${state.status}${detail}`, "pending");
      }
    } else if (state.status === "completed") {
      setSystemCommandStatus(
        `${pendingSystemCommand.label}: erfolgreich abgeschlossen${detail}.`,
        "success"
      );
      if (pendingSystemCommand.command === "ota_update") {
        setOtaStatus(`Firmwareupdate erfolgreich${detail}.`, "success");
        latestOtaManifest = null;
        updateInstallButton();
      }
      pendingSystemCommand = null;
    } else if (state.status === "no_update") {
      setSystemCommandStatus(`${pendingSystemCommand.label}: kein Update nötig${detail}.`, "success");
      if (pendingSystemCommand.command === "ota_update") {
        setOtaStatus(`Kein Update nötig${detail}.`, "success");
      }
      pendingSystemCommand = null;
    } else if (state.status === "error") {
      setSystemCommandStatus(`${pendingSystemCommand.label}: Fehler${detail}.`, "error");
      if (pendingSystemCommand.command === "ota_update") {
        setOtaStatus(`Firmwareupdate fehlgeschlagen${detail}.`, "error");
      }
      pendingSystemCommand = null;
    }
  });
}

async function sendSystemCommand(command, label, confirmText) {
  if (!systemCommandsSupported) {
    alert("Die aktuell erkannte ESP32-Firmware unterstützt die Systemsteuerung nicht.");
    return;
  }

  if (command === "gps_restart" && !gpsSoftwareRestartSupported) {
    alert("Die aktuelle Firmware unterstützt den GPS-Software-Neustart nicht.");
    return;
  }

  if (command === "ota_update" && (!otaSupported || !otaPartitionReady)) {
    alert("Online-Firmwareupdate ist auf diesem ESP32 noch nicht bereit.");
    return;
  }

  if (!confirm(confirmText)) return;

  const requestId = createSystemCommandId();
  pendingSystemCommand = { command, label, requestId };
  setSystemCommandStatus(`${label}: wird gesendet …`, "pending");

  try {
    await set(ref(db, `tracker/config/system_commands/${command}`), {
      requestId,
      requestedAt: Date.now(),
      status: "requested"
    });
  } catch (error) {
    pendingSystemCommand = null;
    setSystemCommandStatus(`${label}: Senden fehlgeschlagen – ${error.message}`, "error");
  }
}

function createSystemCommandId() {
  if (globalThis.crypto && typeof globalThis.crypto.randomUUID === "function") {
    return `${Date.now()}-${globalThis.crypto.randomUUID()}`;
  }
  return `${Date.now()}-${Math.random().toString(16).slice(2)}`;
}

function setSystemCommandStatus(text, state = "") {
  const element = document.getElementById("systemCommandStatus");
  element.textContent = text;
  element.className = "config-status";
  if (state) element.classList.add(`config-status-${state}`);
}

function listenSettings() {
  onValue(ref(db, "tracker/settings"), snapshot => {
    const settings = {
      ...DEFAULT_LIMITS,
      ...(snapshot.val() || {})
    };

    setInput("setBatteryWarn", settings.batteryWarn);
    setInput("setBatteryAlarm", settings.batteryAlarm);
    setInput("setOilPressureWarn", settings.oilPressureWarn);
    setInput("setOilPressureAlarm", settings.oilPressureAlarm);
    setInput("setOilTempWarn", settings.oilTempWarn);
    setInput("setOilTempAlarm", settings.oilTempAlarm);
    setInput("setCylTempWarn", settings.cylTempWarn);
    setInput("setCylTempAlarm", settings.cylTempAlarm);
  });
}

async function saveSettings() {
  const settings = {
    batteryWarn: readInput("setBatteryWarn"),
    batteryAlarm: readInput("setBatteryAlarm"),
    oilPressureWarn: readInput("setOilPressureWarn"),
    oilPressureAlarm: readInput("setOilPressureAlarm"),
    oilTempWarn: readInput("setOilTempWarn"),
    oilTempAlarm: readInput("setOilTempAlarm"),
    cylTempWarn: readInput("setCylTempWarn"),
    cylTempAlarm: readInput("setCylTempAlarm")
  };

  await set(ref(db, "tracker/settings"), settings);
  alert("Alarmgrenzen gespeichert.");
}

function listenOutputConfig() {
  const outputRef = ref(db, "tracker/config/external_output");

  onValue(
    outputRef,
    async snapshot => {
      const stored = snapshot.val();

      if (!stored) {
        await set(outputRef, DEFAULT_OUTPUT_CONFIG);
        return;
      }

      const values = {
        ...DEFAULT_OUTPUT_CONFIG,
        ...stored
      };

      setInput("setOutputSpeedEnableKmh", values.speed_enable_kmh);
      setInput("setOutputRpmOn", values.rpm_on);
      setInput("setOutputRpmOff", values.rpm_off);

      setOutputConfigStatus(
        "Schaltausgang aus Firebase geladen.",
        "success"
      );
    },
    error => {
      setOutputConfigStatus(
        "Firebase-Lesefehler: " + error.message,
        "error"
      );
    }
  );
}

async function saveOutputConfig() {
  try {
    const speedEnableKmh = readBoundedInteger(
      "setOutputSpeedEnableKmh",
      0,
      200
    );

    const rpmOn = readBoundedInteger(
      "setOutputRpmOn",
      0,
      10000
    );

    const rpmOff = readBoundedInteger(
      "setOutputRpmOff",
      0,
      10000
    );

    if (rpmOff >= rpmOn) {
      throw new Error(
        "Der LOW-Ausschaltwert muss kleiner als der HIGH-Einschaltwert sein."
      );
    }

    const outputConfig = {
      speed_enable_kmh: speedEnableKmh,
      rpm_on: rpmOn,
      rpm_off: rpmOff
    };

    setOutputConfigStatus("Wird gespeichert…", "pending");

    await set(
      ref(db, "tracker/config/external_output"),
      outputConfig
    );

    setOutputConfigStatus(
      "Schaltausgang gespeichert.",
      "success"
    );
  } catch (error) {
    setOutputConfigStatus(error.message, "error");
    alert(error.message);
  }
}

function setOutputConfigStatus(text, state = "") {
  const element = document.getElementById("outputConfigStatus");

  element.textContent = text;
  element.className = "config-status";

  if (state) {
    element.classList.add(`config-status-${state}`);
  }
}

function listenIntervals() {
  const intervalsRef = ref(db, "tracker/config/intervals");

  onValue(
    intervalsRef,
    async snapshot => {
      const stored = snapshot.val();

      if (!stored) {
        await set(intervalsRef, DEFAULT_INTERVALS);
        return;
      }

      const values = {
        ...DEFAULT_INTERVALS,
        ...stored
      };

      for (const [key, rule] of Object.entries(INTERVAL_RULES)) {
        setInput(rule.id, values[key]);
      }

      setIntervalStatus(
        "Intervalle aus Firebase geladen.",
        "success"
      );
    },
    error => {
      setIntervalStatus(
        "Firebase-Lesefehler: " + error.message,
        "error"
      );
    }
  );
}

async function saveIntervals() {
  try {
    const intervals = {};

    for (const [key, rule] of Object.entries(INTERVAL_RULES)) {
      intervals[key] = readBoundedInteger(
        rule.id,
        rule.min,
        rule.max
      );
    }

    setIntervalStatus("Wird gespeichert…", "pending");

    await set(
      ref(db, "tracker/config/intervals"),
      intervals
    );

    setIntervalStatus(
      "Intervalle gespeichert.",
      "success"
    );
  } catch (error) {
    setIntervalStatus(error.message, "error");
    alert(error.message);
  }
}

function readBoundedInteger(id, min, max) {
  const input = document.getElementById(id);
  const value = Number(input.value);

  if (!Number.isInteger(value)) {
    throw new Error("Bitte nur ganze Zahlen eingeben.");
  }

  if (value < min || value > max) {
    throw new Error(
      `Der Wert muss zwischen ${min} und ${max} liegen.`
    );
  }

  return value;
}

function setIntervalStatus(text, state = "") {
  const element = document.getElementById("intervalStatus");

  element.textContent = text;
  element.className = "config-status";

  if (state) {
    element.classList.add(`config-status-${state}`);
  }
}

function listenHistorySupport() {
  onValue(ref(db, "tracker/device/historySupported"), snapshot => {
    historySupported = snapshot.val() === true;

    const badge = document.getElementById("historySupportStatus");

    badge.textContent = historySupported
      ? "bereit"
      : "noch nicht unterstützt";

    badge.className =
      "recording-badge " +
      (
        historySupported
          ? "recording-badge-on"
          : "recording-badge-wait"
      );

    updateRecordingButtons();
  });
}

function listenRecordingState() {
  onValue(ref(db, "tracker/config/recording"), snapshot => {
    recordingState = snapshot.val() || {
      enabled: false
    };

    const active = recordingState.enabled === true;
    const badge = document.getElementById("recordingStatus");

    badge.textContent = active ? "läuft" : "aus";

    badge.className =
      "recording-badge " +
      (
        active
          ? "recording-badge-on"
          : "recording-badge-off"
      );

    document.getElementById("currentRaceId").value =
      recordingState.raceId || "";

    if (
      recordingState.raceName &&
      !document.getElementById("raceName").value
    ) {
      document.getElementById("raceName").value =
        recordingState.raceName;
    }

    updateRecordingButtons();
    updateAnalysisDeleteButton();
  });
}

function updateRecordingButtons() {
  const active = recordingState.enabled === true;

  document.getElementById("startRecording").disabled =
    !historySupported || active;

  document.getElementById("stopRecording").disabled =
    !active;

  const hint = document.getElementById("recordingHint");

  if (!historySupported) {
    hint.innerHTML =
      'Die Website ist fertig vorbereitet. Die Aufzeichnung kann erst gestartet werden, ' +
      'wenn die spätere ESP32-Firmware <code>tracker/device/historySupported = true</code> meldet.';
  } else if (active) {
    hint.textContent =
      `Aufzeichnung "${recordingState.raceName || recordingState.raceId}" läuft.`;
  } else {
    hint.textContent =
      "ESP32 unterstützt die Rennhistorie. Eine neue Aufzeichnung kann gestartet werden.";
  }
}

async function startRecording() {
  if (!historySupported) {
    alert(
      "Die aktuelle ESP32-Firmware unterstützt die Rennhistorie noch nicht."
    );
    return;
  }

  const nameInput = document.getElementById("raceName");
  const raceName = nameInput.value.trim();

  if (!raceName) {
    alert("Bitte zuerst einen Rennnamen eingeben.");
    nameInput.focus();
    return;
  }

  const historyInterval = readBoundedInteger(
    "setHistoryUpdateMs",
    1000,
    60000
  );

  const startedAt = Date.now();
  const raceId = createRaceId(startedAt);

  try {
    await set(
      ref(db, `tracker/races/${raceId}`),
      {
        name: raceName,
        startedAt,
        stoppedAt: null,
        status: "recording",
        history_update_ms: historyInterval
      }
    );

    await set(
      ref(db, "tracker/config/recording"),
      {
        enabled: true,
        raceId,
        raceName,
        history_update_ms: historyInterval,
        requestedAt: startedAt
      }
    );

    alert(
      `Rennaufzeichnung "${raceName}" gestartet.`
    );
  } catch (error) {
    alert(
      "Start fehlgeschlagen: " + error.message
    );
  }
}

async function stopRecording() {
  if (
    !recordingState ||
    recordingState.enabled !== true ||
    !recordingState.raceId
  ) {
    alert(
      "Es läuft derzeit keine Rennaufzeichnung."
    );
    return;
  }

  const stoppedAt = Date.now();
  const raceId = recordingState.raceId;

  try {
    await set(
      ref(db, `tracker/races/${raceId}/stoppedAt`),
      stoppedAt
    );

    await set(
      ref(db, `tracker/races/${raceId}/status`),
      "finished"
    );

    await set(
      ref(db, "tracker/config/recording"),
      {
        ...recordingState,
        enabled: false,
        stoppedAt,
        requestedAt: stoppedAt
      }
    );

    alert("Rennaufzeichnung gestoppt.");
  } catch (error) {
    alert(
      "Stop fehlgeschlagen: " + error.message
    );
  }
}

function createRaceId(timestamp) {
  const d = new Date(timestamp);

  const p = n =>
    String(n).padStart(2, "0");

  return (
    `race_${d.getFullYear()}` +
    `${p(d.getMonth() + 1)}` +
    `${p(d.getDate())}_` +
    `${p(d.getHours())}` +
    `${p(d.getMinutes())}` +
    `${p(d.getSeconds())}`
  );
}

async function resetMaxValues() {
  if (!systemCommandsSupported || !maxValuesDeviceOwned) {
    alert("Maximalwerte werden erst ab der ESP32-Firmware V5.9.13 zentral vom Gerät verwaltet.");
    return;
  }

  await sendSystemCommand(
    "max_values_reset",
    "Maximalwerte zurücksetzen",
    "Maximalwerte wirklich zurücksetzen? Der ESP32 beginnt danach sofort mit einer neuen Erfassung."
  );
}

async function clearAlarmHistory() {
  if (!systemCommandsSupported || !alarmHistoryDeviceOwned) {
    alert("Die Alarmhistorie wird erst ab der ESP32-Firmware V5.9.13 zentral vom Gerät verwaltet.");
    return;
  }

  await sendSystemCommand(
    "alarm_history_clear",
    "Alarmhistorie leeren",
    "Alarmhistorie wirklich leeren? Noch lokal gepufferte Alarmereignisse werden dabei ebenfalls gelöscht."
  );
}


function listenMaxValues() {
  onValue(ref(db, "tracker/maxValues"), snapshot => {
    const m = snapshot.val() || {};

    setText(
      "maxSpeed",
      m.maxSpeed != null
        ? Number(m.maxSpeed).toFixed(1)
        : "---"
    );

    setText(
      "maxRpm",
      m.maxRpm != null
        ? Math.round(m.maxRpm)
        : "---"
    );

    setText(
      "maxOilTemp",
      m.maxOilTemp != null
        ? Math.round(m.maxOilTemp)
        : "---"
    );

    setText(
      "maxCylTemp",
      m.maxCylTemp != null
        ? Math.round(m.maxCylTemp)
        : "---"
    );

    setText(
      "minOilPressure",
      m.minOilPressure != null
        ? Number(m.minOilPressure).toFixed(1)
        : "---"
    );

    setText(
      "minBattery",
      m.minBattery != null
        ? Number(m.minBattery).toFixed(1)
        : "---"
    );
  });
}

function listenAlarmHistory() {
  onValue(ref(db, "tracker/alarmHistory"), snapshot => {
    const history = normalizeAdminAlarmHistory(snapshot.val());
    const container = document.getElementById("alarmHistory");

    if (!history.length) {
      container.innerHTML = '<div class="empty-history">Noch keine Alarme.</div>';
      return;
    }

    container.innerHTML = history.map(entry => `
      <div class="alarm-entry ${entry.level === "warning" ? "warning-entry" : ""}">
        <div class="alarm-time">${formatAdminAlarmTime(entry)}</div>
        <div class="alarm-message">${entry.text || "Alarm"}</div>
      </div>
    `).join("");
  });
}

function normalizeAdminAlarmHistory(raw) {
  let history = [];
  if (Array.isArray(raw)) {
    history = raw.filter(Boolean);
  } else if (raw && typeof raw === "object") {
    history = Object.values(raw).filter(x => x && typeof x === "object");
  }

  return history
    .map((entry, index) => ({ ...entry, __order: index }))
    .sort((a, b) => {
      const ta = Number(a.timestamp || 0);
      const tb = Number(b.timestamp || 0);
      if (tb !== ta) return tb - ta;
      const sa = Number(a.sequence || 0);
      const sb = Number(b.sequence || 0);
      if (sb !== sa) return sb - sa;
      return a.__order - b.__order;
    })
    .slice(0, 30);
}

function formatAdminAlarmTime(entry) {
  const timestamp = Number(entry.timestamp || 0);
  if (Number.isFinite(timestamp) && timestamp > 0) {
    return new Date(timestamp).toLocaleString("de-AT");
  }
  if (entry.time) return entry.time;
  const uptime = Number(entry.capturedUptimeMs);
  if (Number.isFinite(uptime) && uptime >= 0) {
    return `Uptime ${Math.round(uptime / 1000)} s`;
  }
  return "---";
}



// ==================================================
// RENNAUSWERTUNG / LÖSCHEN VON RENNAUFZEICHNUNGEN
// ==================================================
function initRaceAnalysis() {
  if (analysisInitialized) return;
  analysisInitialized = true;

  analysisRaceSelect = document.getElementById("raceSelect");
  analysisFromTime = document.getElementById("fromTime");
  analysisToTime = document.getElementById("toTime");
  analysisLoadRangeButton = document.getElementById("loadRange");
  analysisLoadFullButton = document.getElementById("loadFullRace");
  analysisExportButton = document.getElementById("exportCsv");
  analysisDeleteButton = document.getElementById("deleteRace");

  if (!analysisRaceSelect || !analysisLoadRangeButton || !analysisLoadFullButton ||
      !analysisExportButton || !analysisDeleteButton) {
    console.warn("Rennauswertung: benötigte Admin-Elemente fehlen.");
    return;
  }

  analysisRaceSelect.addEventListener("change", analysisOnRaceChange);
  analysisLoadRangeButton.addEventListener("click", analysisLoadSelectedRange);
  analysisLoadFullButton.addEventListener("click", analysisLoadFullRace);
  analysisExportButton.addEventListener("click", analysisExportCsv);
  analysisDeleteButton.addEventListener("click", deleteSelectedRace);
  document.querySelectorAll(".metric-toggle").forEach(cb =>
    cb.addEventListener("change", analysisRenderCharts)
  );

  if (typeof Chart === "undefined") {
    analysisSetStatus("Chart.js konnte nicht geladen werden.", "error");
  }

  loadAnalysisSettings();
  listenAnalysisRaces();
}

async function loadAnalysisSettings() {
  try {
    const snapshot = await get(ref(db, "tracker/settings"));
    analysisSettings = snapshot.val() || {};
  } catch (error) {
    console.warn("Rennauswertung: Alarmgrenzen konnten nicht geladen werden:", error);
  }
}

function listenAnalysisRaces() {
  analysisSetStatus("Lade Rennaufzeichnungen…", "pending");

  onValue(
    ref(db, "tracker/races"),
    snapshot => {
      analysisRaces = snapshot.val() || {};
      populateAnalysisRaceSelect();
    },
    error => {
      analysisSetStatus("Rennaufzeichnungen konnten nicht geladen werden: " + error.message, "error");
    }
  );
}

function populateAnalysisRaceSelect() {
  const entries = Object.entries(analysisRaces)
    .sort((a, b) => Number(b[1]?.startedAt || 0) - Number(a[1]?.startedAt || 0));

  const previous = analysisRaceSelect.value || analysisCurrentRaceId;
  analysisRaceSelect.innerHTML = "";

  if (!entries.length) {
    analysisRaceSelect.innerHTML = '<option value="">Noch keine Rennaufzeichnung vorhanden</option>';
    analysisRaceSelect.disabled = true;
    analysisLoadRangeButton.disabled = true;
    analysisLoadFullButton.disabled = true;
    analysisExportButton.disabled = true;
    analysisDeleteButton.disabled = true;
    analysisCurrentRaceId = "";
    analysisCurrentSamples = [];
    document.getElementById("raceInfo").textContent = "Noch keine Rennaufzeichnung vorhanden.";
    document.getElementById("sampleCount").textContent = "0 Datensätze";
    analysisRenderCharts();
    analysisRenderStats();
    analysisSetStatus("Noch keine Rennen in Firebase.", "pending");
    return;
  }

  for (const [id, meta] of entries) {
    const option = document.createElement("option");
    option.value = id;
    option.textContent = `${meta.name || id} · ${analysisFormatDateTime(meta.startedAt)}`;
    analysisRaceSelect.appendChild(option);
  }

  analysisRaceSelect.disabled = false;
  analysisLoadRangeButton.disabled = false;
  analysisLoadFullButton.disabled = false;

  analysisRaceSelect.value = entries.some(([id]) => id === previous)
    ? previous
    : entries[0][0];

  analysisSetStatus(`${entries.length} Rennaufzeichnung${entries.length === 1 ? "" : "en"} gefunden.`, "success");
  analysisOnRaceChange();
}

function analysisOnRaceChange() {
  analysisCurrentRaceId = analysisRaceSelect.value;
  analysisCurrentSamples = [];
  analysisExportButton.disabled = true;
  document.getElementById("sampleCount").textContent = "0 Datensätze";
  analysisRenderCharts();
  analysisRenderStats();

  const meta = analysisRaces[analysisCurrentRaceId];
  if (!meta) {
    document.getElementById("raceInfo").textContent = "Noch kein Rennen ausgewählt.";
    updateAnalysisDeleteButton();
    return;
  }

  const start = Number(meta.startedAt || Date.now());
  const stop = Number(meta.stoppedAt || Date.now());
  analysisFromTime.value = analysisToLocalInputValue(start);
  analysisToTime.value = analysisToLocalInputValue(stop);

  const statusText = meta.stoppedAt
    ? "Ende: " + analysisFormatDateTime(stop)
    : "Aufzeichnung läuft / kein Endzeitpunkt gespeichert";

  document.getElementById("raceInfo").innerHTML =
    `<strong>${analysisEscapeHtml(meta.name || analysisCurrentRaceId)}</strong> · ` +
    `Start: ${analysisFormatDateTime(start)} · ${statusText} · ` +
    `Archivintervall: ${Number(meta.history_update_ms || 5000) / 1000} s · ` +
    `ID: <code>${analysisEscapeHtml(analysisCurrentRaceId)}</code>`;

  updateAnalysisDeleteButton();
}

function updateAnalysisDeleteButton() {
  if (!analysisDeleteButton || !analysisRaceSelect) return;
  const raceId = analysisRaceSelect.value;
  const active = recordingState?.enabled === true && recordingState?.raceId === raceId;
  const offlinePending = offlineHistoryPendingCount > 0;
  analysisDeleteButton.disabled = !raceId || active || offlinePending;
  analysisDeleteButton.title = active
    ? "Eine laufende Aufzeichnung muss zuerst gestoppt werden."
    : offlinePending
      ? "Löschen ist gesperrt, solange der ESP32 noch Offline-Renndaten nachsendet."
      : "Ausgewählte Rennaufzeichnung vollständig löschen";
}

async function analysisLoadFullRace() {
  const meta = analysisRaces[analysisRaceSelect.value];
  if (!meta) return;

  const start = Number(meta.startedAt || 0);
  const stop = Number(meta.stoppedAt || Date.now());
  analysisFromTime.value = analysisToLocalInputValue(start);
  analysisToTime.value = analysisToLocalInputValue(stop);
  await analysisLoadRange(start, stop);
}

async function analysisLoadSelectedRange() {
  const start = new Date(analysisFromTime.value).getTime();
  const stop = new Date(analysisToTime.value).getTime();

  if (!Number.isFinite(start) || !Number.isFinite(stop)) {
    analysisSetStatus("Bitte gültigen Start- und Endzeitpunkt eingeben.", "error");
    return;
  }
  if (stop <= start) {
    analysisSetStatus("Der Endzeitpunkt muss nach dem Start liegen.", "error");
    return;
  }

  await analysisLoadRange(start, stop);
}

async function analysisLoadRange(start, stop) {
  const raceId = analysisRaceSelect.value;
  if (!raceId) return;

  analysisSetStatus("Historische Daten werden geladen…", "pending");
  analysisLoadRangeButton.disabled = true;
  analysisLoadFullButton.disabled = true;
  analysisExportButton.disabled = true;

  try {
    const historyQuery = query(
      ref(db, `tracker/races/${raceId}/samples`),
      orderByChild("timestamp"),
      startAt(start),
      endAt(stop)
    );

    const snapshot = await get(historyQuery);
    const raw = snapshot.val() || {};

    analysisCurrentSamples = Object.values(raw)
      .filter(sample => sample && Number.isFinite(Number(sample.timestamp)))
      .map(analysisNormalizeSample)
      .sort((a, b) => a.timestamp - b.timestamp);

    document.getElementById("sampleCount").textContent =
      `${analysisCurrentSamples.length.toLocaleString("de-AT")} Datensätze`;

    if (!analysisCurrentSamples.length) {
      analysisSetStatus("Für diesen Zeitraum sind keine historischen Daten gespeichert.", "pending");
      analysisRenderCharts();
      analysisRenderStats();
      return;
    }

    analysisSetStatus(
      `${analysisCurrentSamples.length.toLocaleString("de-AT")} Datensätze geladen · ` +
      `${analysisFormatDateTime(analysisCurrentSamples[0].timestamp)} bis ` +
      `${analysisFormatDateTime(analysisCurrentSamples.at(-1).timestamp)}`,
      "success"
    );

    analysisExportButton.disabled = false;
    analysisRenderCharts();
    analysisRenderStats();
  } catch (error) {
    analysisSetStatus("Historie konnte nicht geladen werden: " + error.message, "error");
  } finally {
    analysisLoadRangeButton.disabled = false;
    analysisLoadFullButton.disabled = false;
    updateAnalysisDeleteButton();
  }
}

function analysisNormalizeSample(sample) {
  const normalized = { timestamp: Number(sample.timestamp) };
  for (const key of Object.keys(ANALYSIS_METRICS)) {
    const n = Number(sample[key]);
    normalized[key] = Number.isFinite(n) ? n : null;
  }
  return normalized;
}

function analysisSelectedMetrics(group) {
  return [...document.querySelectorAll(".metric-toggle:checked")]
    .map(cb => cb.value)
    .filter(key => ANALYSIS_METRICS[key]?.group === group);
}

function analysisRenderCharts() {
  if (typeof Chart === "undefined") return;
  analysisRenderTemperatureChart();
  analysisRenderOperatingChart();
}

function analysisRenderTemperatureChart() {
  const canvas = document.getElementById("temperatureHistoryChart");
  if (!canvas) return;
  const selected = analysisSelectedMetrics("temperature");
  const datasets = selected.map(key => {
    const metric = ANALYSIS_METRICS[key];
    return {
      label: `${metric.label} (${metric.unit})`,
      data: analysisCurrentSamples
        .filter(s => s[key] != null)
        .map(s => ({ x: s.timestamp, y: s[key] })),
      borderColor: metric.color,
      backgroundColor: metric.color,
      borderWidth: 2,
      pointRadius: 0,
      pointHoverRadius: 4,
      tension: 0.08,
      spanGaps: false,
      parsing: false
    };
  });

  if (analysisTemperatureChart) analysisTemperatureChart.destroy();
  analysisTemperatureChart = new Chart(canvas, {
    type: "line",
    data: { datasets },
    options: analysisBaseChartOptions({
      y: {
        type: "linear",
        position: "left",
        title: { display: true, text: "Temperatur (°C)", color: "#bbb" },
        ticks: { color: "#bbb" },
        grid: { color: "#333" }
      }
    })
  });
}

function analysisRenderOperatingChart() {
  const canvas = document.getElementById("operatingHistoryChart");
  if (!canvas) return;
  const selected = analysisSelectedMetrics("operating");
  const datasets = selected.map(key => {
    const metric = ANALYSIS_METRICS[key];
    return {
      label: `${metric.label} (${metric.unit})`,
      data: analysisCurrentSamples
        .filter(s => s[key] != null)
        .map(s => ({ x: s.timestamp, y: s[key] })),
      borderColor: metric.color,
      backgroundColor: metric.color,
      borderWidth: 2,
      pointRadius: 0,
      pointHoverRadius: 4,
      tension: 0.05,
      spanGaps: false,
      parsing: false,
      yAxisID: metric.axis
    };
  });

  if (analysisOperatingChart) analysisOperatingChart.destroy();
  analysisOperatingChart = new Chart(canvas, {
    type: "line",
    data: { datasets },
    options: analysisBaseChartOptions({
      yPressure: analysisAxisOptions("Öldruck (bar)", "left", "#2e9bff"),
      yRpm: analysisAxisOptions("Drehzahl (U/min)", "right", "#a04cff"),
      ySpeed: analysisAxisOptions("Geschwindigkeit (km/h)", "right", "#43ff5f")
    })
  });
}

function analysisBaseChartOptions(yScales) {
  return {
    responsive: true,
    maintainAspectRatio: false,
    animation: false,
    normalized: true,
    parsing: false,
    interaction: { mode: "nearest", intersect: false },
    plugins: {
      legend: { labels: { color: "#ddd" } },
      tooltip: {
        callbacks: {
          title(items) {
            if (!items.length) return "";
            return analysisFormatDateTime(items[0].parsed.x, true);
          }
        }
      },
      decimation: { enabled: true, algorithm: "lttb", samples: 1600 }
    },
    scales: {
      x: {
        type: "linear",
        ticks: {
          color: "#aaa",
          maxTicksLimit: 10,
          callback(value) { return analysisFormatAxisTime(value); }
        },
        grid: { color: "#2a2a2a" },
        title: { display: true, text: "Zeit", color: "#bbb" }
      },
      ...yScales
    }
  };
}

function analysisAxisOptions(title, position, color) {
  return {
    type: "linear",
    position,
    display: "auto",
    title: { display: true, text: title, color },
    ticks: { color },
    grid: { drawOnChartArea: position === "left", color: "#333" }
  };
}

function analysisRenderStats() {
  const container = document.getElementById("statsGrid");
  if (!container) return;

  if (!analysisCurrentSamples.length) {
    container.innerHTML = '<div class="empty-history">Noch keine historischen Daten geladen.</div>';
    return;
  }

  container.innerHTML = Object.entries(ANALYSIS_METRICS).map(([key, metric]) => {
    const values = analysisCurrentSamples
      .filter(s => s[key] != null)
      .map(s => ({ timestamp: s.timestamp, value: s[key] }));

    if (!values.length) {
      return `<div class="stat-card"><div class="stat-title">${metric.label}</div><div class="stat-empty">Keine Daten</div></div>`;
    }

    const rawValues = values.map(v => v.value);
    const min = Math.min(...rawValues);
    const max = Math.max(...rawValues);
    const avg = rawValues.reduce((sum, v) => sum + v, 0) / rawValues.length;
    const maxPoint = values.reduce((best, p) => p.value > best.value ? p : best);
    const thresholdText = analysisThresholdDurationText(key, values);

    return `
      <div class="stat-card">
        <div class="stat-title">${metric.label}</div>
        <div class="stat-row"><span>Minimum</span><strong>${analysisFormatMetric(min, metric)} ${metric.unit}</strong></div>
        <div class="stat-row"><span>Maximum</span><strong>${analysisFormatMetric(max, metric)} ${metric.unit}</strong></div>
        <div class="stat-row"><span>Durchschnitt</span><strong>${analysisFormatMetric(avg, metric)} ${metric.unit}</strong></div>
        <div class="stat-row"><span>Maximum am</span><strong>${analysisFormatDateTime(maxPoint.timestamp)}</strong></div>
        ${thresholdText}
      </div>`;
  }).join("");
}

function analysisThresholdDurationText(key, values) {
  let warn = null;
  let alarm = null;
  let direction = "high";

  if (key === "oil_temp") {
    warn = analysisNumberOrNull(analysisSettings.oilTempWarn);
    alarm = analysisNumberOrNull(analysisSettings.oilTempAlarm);
  } else if (key === "cylinder_temp") {
    warn = analysisNumberOrNull(analysisSettings.cylTempWarn);
    alarm = analysisNumberOrNull(analysisSettings.cylTempAlarm);
  } else if (key === "oil_pressure") {
    warn = analysisNumberOrNull(analysisSettings.oilPressureWarn);
    alarm = analysisNumberOrNull(analysisSettings.oilPressureAlarm);
    direction = "low";
  } else {
    return '<div class="stat-row"><span>Grenzzeit</span><strong>—</strong></div>';
  }

  if (warn == null || alarm == null) {
    return '<div class="stat-row"><span>Grenzzeit</span><strong>Grenzen fehlen</strong></div>';
  }

  const warnMs = analysisDurationBeyond(values, warn, direction);
  const alarmMs = analysisDurationBeyond(values, alarm, direction);
  return `
    <div class="stat-row"><span>Warnbereich</span><strong>${analysisFormatDuration(warnMs)}</strong></div>
    <div class="stat-row"><span>Alarmbereich</span><strong>${analysisFormatDuration(alarmMs)}</strong></div>`;
}

function analysisDurationBeyond(values, threshold, direction) {
  if (values.length < 2) return 0;
  const intervals = [];
  for (let i = 1; i < values.length; i++) {
    const dt = values[i].timestamp - values[i - 1].timestamp;
    if (dt > 0 && dt < 60000) intervals.push(dt);
  }
  const sorted = [...intervals].sort((a, b) => a - b);
  const median = sorted.length ? sorted[Math.floor(sorted.length / 2)] : 5000;
  const maxGap = Math.max(15000, median * 3);
  let total = 0;

  for (let i = 0; i < values.length - 1; i++) {
    const current = values[i];
    const dt = values[i + 1].timestamp - current.timestamp;
    if (dt <= 0 || dt > maxGap) continue;
    const beyond = direction === "low"
      ? current.value <= threshold
      : current.value >= threshold;
    if (beyond) total += dt;
  }
  return total;
}

async function deleteSelectedRace() {
  const raceId = analysisRaceSelect?.value;
  if (!raceId) return;

  const meta = analysisRaces[raceId] || {};
  const active = recordingState?.enabled === true && recordingState?.raceId === raceId;
  if (active) {
    alert("Diese Rennaufzeichnung läuft gerade. Bitte zuerst die Aufzeichnung stoppen.");
    return;
  }

  const raceName = meta.name || raceId;
  const confirmed = confirm(
    `Rennaufzeichnung "${raceName}" wirklich endgültig löschen?\n\n` +
    `Gelöscht werden:\n- Renninformationen\n- alle historischen Messwerte dieses Rennens\n\n` +
    `Dieser Vorgang kann nicht rückgängig gemacht werden.`
  );
  if (!confirmed) return;

  analysisDeleteButton.disabled = true;
  analysisSetStatus(`Lösche "${raceName}" …`, "pending");

  try {
    const updates = {};
    updates[`tracker/races/${raceId}`] = null;
    updates[`tracker/history/${raceId}`] = null;

    // Falls dies der zuletzt verwendete, aber bereits gestoppte Lauf war,
    // werden auch die veralteten Verweise in der Recording-Konfiguration entfernt.
    if (recordingState?.enabled !== true && recordingState?.raceId === raceId) {
      updates["tracker/config/recording/raceId"] = null;
      updates["tracker/config/recording/raceName"] = null;
      updates["tracker/config/recording/stoppedAt"] = null;
    }

    await update(ref(db), updates);

    if (analysisCurrentRaceId === raceId) {
      analysisCurrentRaceId = "";
      analysisCurrentSamples = [];
      analysisExportButton.disabled = true;
      document.getElementById("sampleCount").textContent = "0 Datensätze";
      analysisRenderCharts();
      analysisRenderStats();
    }

    analysisSetStatus(`Rennaufzeichnung "${raceName}" vollständig gelöscht.`, "success");
  } catch (error) {
    analysisSetStatus("Löschen fehlgeschlagen: " + error.message, "error");
    alert("Rennaufzeichnung konnte nicht gelöscht werden: " + error.message);
    updateAnalysisDeleteButton();
  }
}

function analysisExportCsv() {
  if (!analysisCurrentSamples.length) return;
  const meta = analysisRaces[analysisRaceSelect.value] || {};
  const header = [
    "Zeitpunkt", "timestamp", "Zylinderkopftemperatur_C", "Motoroeltemperatur_C",
    "Getriebeoeltemperatur_C", "Oeldruck_bar", "Drehzahl_Umin", "Geschwindigkeit_kmh"
  ];
  const rows = analysisCurrentSamples.map(s => [
    analysisFormatDateTime(s.timestamp, true), s.timestamp,
    analysisCsvNumber(s.cylinder_temp), analysisCsvNumber(s.oil_temp),
    analysisCsvNumber(s.gear_oil_temp), analysisCsvNumber(s.oil_pressure),
    analysisCsvNumber(s.rpm), analysisCsvNumber(s.speed_kmh)
  ]);
  const csv = "\ufeff" + [header, ...rows]
    .map(row => row.map(analysisCsvEscape).join(";"))
    .join("\r\n");
  const blob = new Blob([csv], { type: "text/csv;charset=utf-8" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = `${analysisSafeFilename(meta.name || analysisRaceSelect.value || "MF35X_Rennen")}_Auswertung.csv`;
  document.body.appendChild(a);
  a.click();
  a.remove();
  URL.revokeObjectURL(url);
}

function analysisCsvNumber(value) {
  return value == null ? "" : String(value).replace(".", ",");
}

function analysisCsvEscape(value) {
  const text = String(value ?? "");
  return /[;"\r\n]/.test(text) ? `"${text.replaceAll('"', '""')}"` : text;
}

function analysisSafeFilename(text) {
  return String(text).replace(/[<>:"/\\|?*\x00-\x1F]/g, "_").trim() || "MF35X_Rennen";
}

function analysisNumberOrNull(value) {
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}

function analysisFormatMetric(value, metric) {
  return Number(value).toLocaleString("de-AT", {
    minimumFractionDigits: metric.decimals,
    maximumFractionDigits: metric.decimals
  });
}

function analysisFormatDuration(ms) {
  if (!ms) return "0 min";
  const totalMinutes = Math.round(ms / 60000);
  const hours = Math.floor(totalMinutes / 60);
  const minutes = totalMinutes % 60;
  return hours ? `${hours} h ${minutes} min` : `${minutes} min`;
}

function analysisFormatDateTime(timestamp, seconds = false) {
  if (!timestamp) return "---";
  return new Date(Number(timestamp)).toLocaleString("de-AT", {
    day: "2-digit", month: "2-digit", year: "numeric",
    hour: "2-digit", minute: "2-digit",
    ...(seconds ? { second: "2-digit" } : {})
  });
}

function analysisFormatAxisTime(timestamp) {
  const d = new Date(Number(timestamp));
  const p = n => String(n).padStart(2, "0");
  return `${p(d.getDate())}.${p(d.getMonth() + 1)} ${p(d.getHours())}:${p(d.getMinutes())}`;
}

function analysisToLocalInputValue(timestamp) {
  const d = new Date(Number(timestamp));
  const p = n => String(n).padStart(2, "0");
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}T${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`;
}

function analysisSetStatus(text, state = "") {
  const el = document.getElementById("analysisStatus");
  if (!el) return;
  el.textContent = text;
  el.className = "config-status";
  if (state) el.classList.add(`config-status-${state}`);
}

function analysisEscapeHtml(text) {
  return String(text).replace(/[&<>"']/g, c => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#039;"
  }[c]));
}

function readLiveNumber(value) {
  if (
    value === undefined ||
    value === null ||
    value === ""
  ) {
    return null;
  }

  const number = Number(value);

  return Number.isNaN(number)
    ? null
    : number;
}

function setInput(id, value) {
  document.getElementById(id).value = value;
}

function readInput(id) {
  return Number(
    document.getElementById(id).value
  );
}

function setText(id, value) {
  document.getElementById(id).innerText = value;
}
