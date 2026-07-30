/* MF35X Tracker Admin V9.4.0 – Alarmgrenzen und Aktualisierungsintervalle */
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

const DEFAULT_INTERVALS = {
  rpm_output_update_ms: 10,
  rpm_firebase_update_ms: 250,
  temperature_update_ms: 1000,
  gps_update_ms: 1000
};

const INTERVAL_RULES = {
  rpm_output_update_ms: { inputId: "setRpmOutputUpdateMs", min: 5, max: 200 },
  rpm_firebase_update_ms: { inputId: "setRpmFirebaseUpdateMs", min: 100, max: 5000 },
  temperature_update_ms: { inputId: "setTemperatureUpdateMs", min: 250, max: 10000 },
  gps_update_ms: { inputId: "setGpsUpdateMs", min: 1000, max: 30000 }
};

const app = initializeApp(firebaseConfig);
const db = getDatabase(app);

let adminStarted = false;

document.getElementById("loginButton").addEventListener("click", login);
document.getElementById("adminPassword").addEventListener("keydown", event => {
  if (event.key === "Enter") login();
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
  listenIntervals();
  listenMaxValues();
  listenAlarmHistory();

  document.getElementById("saveSettings").addEventListener("click", saveSettings);

  document.getElementById("resetSettings").addEventListener("click", async () => {
    await set(ref(db, "tracker/settings"), DEFAULT_LIMITS);
    alert("Standardwerte geladen.");
  });

  document.getElementById("saveIntervals").addEventListener("click", saveIntervals);

  document.getElementById("resetIntervals").addEventListener("click", async () => {
    await set(ref(db, "tracker/config/intervals"), DEFAULT_INTERVALS);
    setIntervalStatus("Standardintervalle wurden gespeichert.", "success");
  });

  document.getElementById("resetMaxValues").addEventListener("click", resetMaxValues);

  document.getElementById("clearAlarmHistory").addEventListener("click", async () => {
    await set(ref(db, "tracker/alarmHistory"), []);
    alert("Alarmhistorie geleert.");
  });
}

function listenSettings() {
  onValue(ref(db, "tracker/settings"), snapshot => {
    const settings = { ...DEFAULT_LIMITS, ...(snapshot.val() || {}) };

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

function listenIntervals() {
  const intervalsRef = ref(db, "tracker/config/intervals");

  onValue(
    intervalsRef,
    async snapshot => {
      const stored = snapshot.val();

      if (!stored) {
        try {
          await set(intervalsRef, DEFAULT_INTERVALS);
          return;
        } catch (error) {
          loadIntervalInputs(DEFAULT_INTERVALS);
          setIntervalStatus("Standardwerte angezeigt, aber Firebase konnte nicht initialisiert werden.", "error");
          return;
        }
      }

      const intervals = { ...DEFAULT_INTERVALS, ...stored };
      loadIntervalInputs(intervals);
      setIntervalStatus("Intervalle aus Firebase geladen.", "success");
    },
    error => {
      loadIntervalInputs(DEFAULT_INTERVALS);
      setIntervalStatus("Firebase-Lesefehler: " + error.message, "error");
    }
  );
}

async function saveIntervals() {
  try {
    const intervals = {};

    for (const [key, rule] of Object.entries(INTERVAL_RULES)) {
      intervals[key] = readBoundedInteger(rule.inputId, rule.min, rule.max);
    }

    setIntervalStatus("Wird gespeichert…", "pending");
    await set(ref(db, "tracker/config/intervals"), intervals);
    setIntervalStatus("Intervalle gespeichert.", "success");
  } catch (error) {
    setIntervalStatus(error.message, "error");
    alert(error.message);
  }
}

function loadIntervalInputs(intervals) {
  for (const [key, rule] of Object.entries(INTERVAL_RULES)) {
    setInput(rule.inputId, intervals[key]);
  }
}

function readBoundedInteger(inputId, min, max) {
  const input = document.getElementById(inputId);
  const value = Number(input.value);

  if (!Number.isFinite(value) || !Number.isInteger(value)) {
    throw new Error(`Bitte bei „${input.closest("label").childNodes[0].textContent.trim()}“ eine ganze Zahl eingeben.`);
  }

  if (value < min || value > max) {
    throw new Error(`Der Wert bei „${input.closest("label").childNodes[0].textContent.trim()}“ muss zwischen ${min} und ${max} ms liegen.`);
  }

  return value;
}

function setIntervalStatus(text, state = "") {
  const element = document.getElementById("intervalStatus");
  element.textContent = text;
  element.className = "config-status";

  if (state) element.classList.add(`config-status-${state}`);
}

async function resetMaxValues() {
  const button = document.getElementById("resetMaxValues");
  const originalText = button.innerText;

  button.disabled = true;
  button.innerText = "Wird zurückgesetzt...";

  try {
    const snapshot = await get(ref(db, "tracker/live"));
    const live = snapshot.val() || {};
    const resetAt = Date.now();

    const resetValues = {
      maxSpeed: readLiveNumber(live.speed_kmh),
      maxRpm: readLiveNumber(live.rpm),
      maxOilTemp: readLiveNumber(live.oil_temp),
      maxCylTemp: readLiveNumber(live.cylinder_temp),
      minOilPressure: readLiveNumber(live.oil_pressure),
      minBattery: readLiveNumber(live.battery_v),
      resetAt
    };

    await set(ref(db, "tracker/maxValues"), resetValues);
    alert("Maximalwerte wurden auf die aktuellen Live-Werte zurückgesetzt.");
  } catch (error) {
    alert("Fehler beim Zurücksetzen: " + error.message);
  } finally {
    button.disabled = false;
    button.innerText = originalText;
  }
}

function listenMaxValues() {
  onValue(ref(db, "tracker/maxValues"), snapshot => {
    const maxValues = snapshot.val() || {};

    setText("maxSpeed", maxValues.maxSpeed != null ? Number(maxValues.maxSpeed).toFixed(1) : "---");
    setText("maxRpm", maxValues.maxRpm != null ? Math.round(maxValues.maxRpm) : "---");
    setText("maxOilTemp", maxValues.maxOilTemp != null ? Math.round(maxValues.maxOilTemp) : "---");
    setText("maxCylTemp", maxValues.maxCylTemp != null ? Math.round(maxValues.maxCylTemp) : "---");
    setText("minOilPressure", maxValues.minOilPressure != null ? Number(maxValues.minOilPressure).toFixed(1) : "---");
    setText("minBattery", maxValues.minBattery != null ? Number(maxValues.minBattery).toFixed(1) : "---");
  });
}

function listenAlarmHistory() {
  onValue(ref(db, "tracker/alarmHistory"), snapshot => {
    renderAlarmHistory(snapshot.val() || []);
  });
}

function renderAlarmHistory(history) {
  const container = document.getElementById("alarmHistory");

  if (!history.length) {
    container.innerHTML = '<div class="empty-history">Noch keine Alarme.</div>';
    return;
  }

  container.innerHTML = history.map(entry => `
    <div class="alarm-entry ${entry.level === "warning" ? "warning-entry" : ""}">
      <div class="alarm-time">${entry.time}</div>
      <div class="alarm-message">${entry.text}</div>
    </div>
  `).join("");
}

function readLiveNumber(value) {
  if (value === undefined || value === null || value === "") return null;
  const number = Number(value);
  return Number.isNaN(number) ? null : number;
}

function setInput(id, value) {
  document.getElementById(id).value = value;
}

function readInput(id) {
  return Number(document.getElementById(id).value);
}

function setText(id, value) {
  document.getElementById(id).innerText = value;
}
