/* MF35X Tracker Admin V9.4.2 */
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
  oil_pressure_update_ms: 100,
  temperature_update_ms: 1000,
  gps_update_ms: 1000
};

const INTERVAL_RULES = {
  rpm_output_update_ms: { id: "setRpmOutputUpdateMs", min: 5, max: 200 },
  rpm_firebase_update_ms: { id: "setRpmFirebaseUpdateMs", min: 100, max: 5000 },
  oil_pressure_update_ms: { id: "setOilPressureUpdateMs", min: 50, max: 5000 },
  temperature_update_ms: { id: "setTemperatureUpdateMs", min: 250, max: 10000 },
  gps_update_ms: { id: "setGpsUpdateMs", min: 1000, max: 30000 }
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
    setIntervalStatus("Standardintervalle gespeichert.", "success");
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

  onValue(intervalsRef, async snapshot => {
    const stored = snapshot.val();

    if (!stored) {
      await set(intervalsRef, DEFAULT_INTERVALS);
      return;
    }

    const values = { ...DEFAULT_INTERVALS, ...stored };
    for (const [key, rule] of Object.entries(INTERVAL_RULES)) {
      setInput(rule.id, values[key]);
    }

    setIntervalStatus("Intervalle aus Firebase geladen.", "success");
  }, error => {
    setIntervalStatus("Firebase-Lesefehler: " + error.message, "error");
  });
}

async function saveIntervals() {
  try {
    const intervals = {};

    for (const [key, rule] of Object.entries(INTERVAL_RULES)) {
      intervals[key] = readBoundedInteger(rule.id, rule.min, rule.max);
    }

    setIntervalStatus("Wird gespeichert…", "pending");
    await set(ref(db, "tracker/config/intervals"), intervals);
    setIntervalStatus("Intervalle gespeichert.", "success");
  } catch (error) {
    setIntervalStatus(error.message, "error");
    alert(error.message);
  }
}

function readBoundedInteger(id, min, max) {
  const input = document.getElementById(id);
  const value = Number(input.value);

  if (!Number.isInteger(value)) {
    throw new Error("Bitte nur ganze Millisekundenwerte eingeben.");
  }

  if (value < min || value > max) {
    throw new Error(`Der Wert muss zwischen ${min} und ${max} ms liegen.`);
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

    await set(ref(db, "tracker/maxValues"), {
      maxSpeed: readLiveNumber(live.speed_kmh),
      maxRpm: readLiveNumber(live.rpm),
      maxOilTemp: readLiveNumber(live.oil_temp),
      maxCylTemp: readLiveNumber(live.cylinder_temp),
      minOilPressure: readLiveNumber(live.oil_pressure),
      minBattery: readLiveNumber(live.battery_v),
      resetAt: Date.now()
    });

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
    const m = snapshot.val() || {};
    setText("maxSpeed", m.maxSpeed != null ? Number(m.maxSpeed).toFixed(1) : "---");
    setText("maxRpm", m.maxRpm != null ? Math.round(m.maxRpm) : "---");
    setText("maxOilTemp", m.maxOilTemp != null ? Math.round(m.maxOilTemp) : "---");
    setText("maxCylTemp", m.maxCylTemp != null ? Math.round(m.maxCylTemp) : "---");
    setText("minOilPressure", m.minOilPressure != null ? Number(m.minOilPressure).toFixed(1) : "---");
    setText("minBattery", m.minBattery != null ? Number(m.minBattery).toFixed(1) : "---");
  });
}

function listenAlarmHistory() {
  onValue(ref(db, "tracker/alarmHistory"), snapshot => {
    const history = snapshot.val() || [];
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
  });
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
