/* MF35X Tracker Admin V9.5.4 */

import { initializeApp } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js";
import { getDatabase, ref, onValue, set, get } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-database.js";
import { firebaseConfig } from "./firebase-config.js";

const ADMIN_PASSWORD = "mf35x";

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
let pendingSystemCommand = null;

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

  document.getElementById("clearAlarmHistory").addEventListener("click", async () => {
    await set(ref(db, "tracker/alarmHistory"), []);
    alert("Alarmhistorie geleert.");
  });
}

function listenSystemSupport() {
  onValue(ref(db, "tracker/device"), snapshot => {
    const device = snapshot.val() || {};

    systemCommandsSupported =
      device.systemCommandsSupported === true;

    gpsSoftwareRestartSupported =
      device.gpsSoftwareRestartSupported === true;

    const firmware = device.firmware || "---";
    const firmwareBadge =
      document.getElementById("systemFirmwareStatus");

    firmwareBadge.textContent = firmware;
    firmwareBadge.className =
      "recording-badge " +
      (device.firmware ? "recording-badge-on" : "recording-badge-wait");

    const supportBadge =
      document.getElementById("systemSupportStatus");

    supportBadge.textContent = systemCommandsSupported
      ? "bereit"
      : "nicht unterstützt";

    supportBadge.className =
      "recording-badge " +
      (systemCommandsSupported
        ? "recording-badge-on"
        : "recording-badge-wait");

    document.getElementById("restartEsp32").disabled =
      !systemCommandsSupported;

    document.getElementById("restartWifi").disabled =
      !systemCommandsSupported;

    document.getElementById("restartGps").disabled =
      !systemCommandsSupported || !gpsSoftwareRestartSupported;
  });
}

function listenSystemCommands() {
  onValue(ref(db, "tracker/config/system_commands"), snapshot => {
    if (!pendingSystemCommand) return;

    const commands = snapshot.val() || {};
    const state = commands[pendingSystemCommand.command];

    if (!state || state.requestId !== pendingSystemCommand.requestId) {
      return;
    }

    if (state.status === "requested") {
      setSystemCommandStatus(
        `${pendingSystemCommand.label}: Befehl gesendet – wartet auf ESP32.`,
        "pending"
      );
    } else if (state.status === "restarting") {
      setSystemCommandStatus(
        `${pendingSystemCommand.label}: wird ausgeführt …`,
        "pending"
      );
    } else if (state.status === "completed") {
      setSystemCommandStatus(
        `${pendingSystemCommand.label}: erfolgreich abgeschlossen.`,
        "success"
      );
      pendingSystemCommand = null;
    }
  });
}

async function sendSystemCommand(command, label, confirmText) {
  if (!systemCommandsSupported) {
    alert(
      "Die aktuell erkannte ESP32-Firmware unterstützt die Systemsteuerung noch nicht. Bitte zuerst V5.9.1 aufspielen."
    );
    return;
  }

  if (command === "gps_restart" && !gpsSoftwareRestartSupported) {
    alert("Die aktuelle Firmware unterstützt den GPS-Software-Neustart nicht.");
    return;
  }

  if (!confirm(confirmText)) return;

  const requestId = createSystemCommandId();
  pendingSystemCommand = { command, label, requestId };

  setSystemCommandStatus(`${label}: wird gesendet …`, "pending");

  try {
    await set(
      ref(db, `tracker/config/system_commands/${command}`),
      {
        requestId,
        requestedAt: Date.now(),
        status: "requested"
      }
    );
  } catch (error) {
    pendingSystemCommand = null;
    setSystemCommandStatus(
      `${label}: Senden fehlgeschlagen – ${error.message}`,
      "error"
    );
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

  if (state) {
    element.classList.add(`config-status-${state}`);
  }
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
  const button =
    document.getElementById("resetMaxValues");

  const originalText = button.innerText;

  button.disabled = true;
  button.innerText = "Wird zurückgesetzt...";

  try {
    const snapshot = await get(
      ref(db, "tracker/live")
    );

    const live = snapshot.val() || {};

    await set(
      ref(db, "tracker/maxValues"),
      {
        maxSpeed: readLiveNumber(live.speed_kmh),
        maxRpm: readLiveNumber(live.rpm),
        maxOilTemp: readLiveNumber(live.oil_temp),
        maxCylTemp: readLiveNumber(live.cylinder_temp),
        minOilPressure: readLiveNumber(live.oil_pressure),
        minBattery: readLiveNumber(live.battery_v),
        resetAt: Date.now()
      }
    );

    alert(
      "Maximalwerte wurden auf die aktuellen Live-Werte zurückgesetzt."
    );
  } catch (error) {
    alert(
      "Fehler beim Zurücksetzen: " + error.message
    );
  } finally {
    button.disabled = false;
    button.innerText = originalText;
  }
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
    const history = snapshot.val() || [];
    const container =
      document.getElementById("alarmHistory");

    if (!history.length) {
      container.innerHTML =
        '<div class="empty-history">Noch keine Alarme.</div>';
      return;
    }

    container.innerHTML = history.map(entry => `
      <div class="alarm-entry ${entry.level === "warning" ? "warning-entry" : ""}">
        <div class="alarm-time">${entry.time}</div>
        <div class="alarm-message">${entry.text}</div>
      </div>
    `).join("");
  });
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
