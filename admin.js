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

const app = initializeApp(firebaseConfig);
const db = getDatabase(app);

let adminStarted = false;

document.getElementById("loginButton").addEventListener("click", () => {
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
});

function initAdmin() {
  listenSettings();
  listenMaxValues();
  listenAlarmHistory();

  document.getElementById("saveSettings").addEventListener("click", saveSettings);

  document.getElementById("resetSettings").addEventListener("click", async () => {
    await set(ref(db, "tracker/settings"), DEFAULT_LIMITS);
    alert("Standardwerte geladen.");
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

async function resetMaxValues() {
  const snapshot = await get(ref(db, "tracker/live"));
  const live = snapshot.val() || {};

  const resetValues = {
    maxSpeed: readLiveNumber(live.speed_kmh),
    maxRpm: readLiveNumber(live.rpm),
    maxOilTemp: readLiveNumber(live.oil_temp),
    maxCylTemp: readLiveNumber(live.cylinder_temp),
    minOilPressure: readLiveNumber(live.oil_pressure),
    minBattery: readLiveNumber(live.battery_v),
    resetAt: Date.now()
  };

  await set(ref(db, "tracker/maxValues"), resetValues);
  await set(ref(db, "tracker/maxReset"), { resetAt: Date.now() });

  alert("Maximalwerte wurden zurückgesetzt.");
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
