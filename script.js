import { initializeApp } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js";
import { getDatabase, ref, onValue } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-database.js";

const firebaseConfig = {
  apiKey: "AIzaSyARYH1P-dQ8-tbp4BnhcThHqqNuLaZmUxU",
  authDomain: "tracker-989a9.firebaseapp.com",
  databaseURL: "https://tracker-989a9-default-rtdb.europe-west1.firebasedatabase.app",
  projectId: "tracker-989a9",
  storageBucket: "tracker-989a9.firebasestorage.app",
  messagingSenderId: "523207717217",
  appId: "1:523207717217:web:b7ea5a4e90b0a653aed520"
};

const app = initializeApp(firebaseConfig);
const db = getDatabase(app);

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

let limits = loadLimits();

let map = L.map("map").setView([48.2, 16.3], 15);

L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", {
  maxZoom: 19,
  attribution: "&copy; OpenStreetMap"
}).addTo(map);

let marker = L.marker([48.2, 16.3]).addTo(map);
let firstPosition = true;
let lastDataTime = 0;
let lastValidPosition = null;

setupSettingsUi();

const liveRef = ref(db, "tracker/live");

onValue(liveRef, (snapshot) => {
  const data = snapshot.val();

  if (!data || data.lat === undefined || data.lng === undefined) {
    setOfflineValues("Keine Daten");
    return;
  }

  const lat = Number(data.lat);
  const lng = Number(data.lng);

  if (Number.isNaN(lat) || Number.isNaN(lng)) {
    setOfflineValues("GPS ungültig");
    return;
  }

  lastDataTime = Date.now();
  lastValidPosition = { lat, lng };
  setStatus("Online", true);

  setText("speed", data.speed_kmh !== undefined ? Number(data.speed_kmh).toFixed(1) : "---");
  setText("sat", data.satellites !== undefined ? data.satellites : "---");

  const now = new Date();
  document.getElementById("lastUpdateSmall").innerText = now.toLocaleTimeString("de-AT");

  const battery = readNumber(data.battery_v);
  const rpm = readNumber(data.rpm);
  const oilPressure = readNumber(data.oil_pressure);
  const oilTemp = readNumber(data.oil_temp);
  const cylTemp = readNumber(data.cylinder_temp);

  setText("battery", battery !== null ? battery.toFixed(1) : "---");
  setText("rpm", rpm !== null ? Math.round(rpm) : "---");
  setText("oilpressure", oilPressure !== null ? oilPressure.toFixed(1) : "---");
  setText("oiltemp", oilTemp !== null ? Math.round(oilTemp) : "---");
  setText("cyltemp", cylTemp !== null ? Math.round(cylTemp) : "---");

  applyAlarmState("battery", "batteryIcon", battery, "low", limits.batteryWarn, limits.batteryAlarm);
  applyAlarmState("oilpressure", "oilpressureIcon", oilPressure, "low", limits.oilPressureWarn, limits.oilPressureAlarm);
  applyAlarmState("oiltemp", "oiltempIcon", oilTemp, "high", limits.oilTempWarn, limits.oilTempAlarm);
  applyAlarmState("cyltemp", "cyltempIcon", cylTemp, "high", limits.cylTempWarn, limits.cylTempAlarm);

  marker.setLatLng([lat, lng]);

  if (firstPosition) {
    map.setView([lat, lng], 17);
    firstPosition = false;
  } else {
    map.panTo([lat, lng]);
  }

  const mapsButton = document.getElementById("mapsButton");
  mapsButton.href = "https://www.google.com/maps?q=" + lat + "," + lng;
  mapsButton.classList.remove("disabled");
});

setInterval(() => {
  if (lastDataTime === 0) {
    setOfflineValues("Keine Daten");
    return;
  }

  const ageSeconds = (Date.now() - lastDataTime) / 1000;

  if (ageSeconds > 10) {
    setOfflineValues("Offline");
  } else {
    setStatus("Online", true);
  }
}, 1000);

function setText(id, value) {
  document.getElementById(id).innerText = value;
}

function readNumber(value) {
  if (value === undefined || value === null || value === "") return null;
  const number = Number(value);
  return Number.isNaN(number) ? null : number;
}

function setStatus(text, isOnline) {
  const status = document.getElementById("status");
  const icon = document.getElementById("statusIcon");

  status.innerText = text;
  status.classList.remove("online", "offline");
  icon.classList.remove("online", "offline");

  if (isOnline) {
    status.classList.add("online");
    icon.classList.add("online");
  } else {
    status.classList.add("offline");
    icon.classList.add("offline");
  }
}

function setOfflineValues(statusText) {
  setStatus(statusText, false);

  ["speed", "sat", "battery", "rpm", "oilpressure", "oiltemp", "cyltemp"].forEach(id => {
    setText(id, "---");
  });

  ["battery", "oilpressure", "oiltemp", "cyltemp"].forEach(clearAlarmState);

  if (lastDataTime === 0) {
    document.getElementById("lastUpdateSmall").innerText = "---";
  }

  const mapsButton = document.getElementById("mapsButton");

  if (lastValidPosition) {
    mapsButton.href = "https://www.google.com/maps?q=" + lastValidPosition.lat + "," + lastValidPosition.lng;
    mapsButton.classList.remove("disabled");
  } else {
    mapsButton.href = "#";
    mapsButton.classList.add("disabled");
  }
}

function applyAlarmState(valueId, iconId, value, direction, warnLimit, alarmLimit) {
  const valueElement = document.getElementById(valueId);
  const iconElement = document.getElementById(iconId);
  const card = valueElement.closest(".card");

  clearAlarmState(valueId);

  if (value === null) return;

  let alarm = false;
  let warning = false;

  if (direction === "low") {
    alarm = value <= alarmLimit;
    warning = value <= warnLimit && !alarm;
  } else {
    alarm = value >= alarmLimit;
    warning = value >= warnLimit && !alarm;
  }

  if (alarm) {
    card.classList.add("alarm-card");
    valueElement.classList.add("alarm-color");
    iconElement.classList.add("alarm-color");
  } else if (warning) {
    card.classList.add("warning-card");
    valueElement.classList.add("warn-color");
    iconElement.classList.add("warn-color");
  }
}

function clearAlarmState(valueId) {
  const valueElement = document.getElementById(valueId);
  if (!valueElement) return;

  const card = valueElement.closest(".card");
  const icon = card.querySelector(".icon");

  card.classList.remove("warning-card", "alarm-card");
  valueElement.classList.remove("warn-color", "alarm-color");
  if (icon) icon.classList.remove("warn-color", "alarm-color");
}

function setupSettingsUi() {
  const mapping = {
    setBatteryWarn: "batteryWarn",
    setBatteryAlarm: "batteryAlarm",
    setOilPressureWarn: "oilPressureWarn",
    setOilPressureAlarm: "oilPressureAlarm",
    setOilTempWarn: "oilTempWarn",
    setOilTempAlarm: "oilTempAlarm",
    setCylTempWarn: "cylTempWarn",
    setCylTempAlarm: "cylTempAlarm"
  };

  for (const [inputId, key] of Object.entries(mapping)) {
    document.getElementById(inputId).value = limits[key];
  }

  document.getElementById("saveSettings").addEventListener("click", () => {
    for (const [inputId, key] of Object.entries(mapping)) {
      const value = Number(document.getElementById(inputId).value);
      if (!Number.isNaN(value)) limits[key] = value;
    }

    localStorage.setItem("mf35xAlarmLimits", JSON.stringify(limits));
    alert("Alarmgrenzen gespeichert.");
  });

  document.getElementById("resetSettings").addEventListener("click", () => {
    limits = { ...DEFAULT_LIMITS };
    localStorage.setItem("mf35xAlarmLimits", JSON.stringify(limits));

    for (const [inputId, key] of Object.entries(mapping)) {
      document.getElementById(inputId).value = limits[key];
    }

    alert("Standardwerte geladen.");
  });
}

function loadLimits() {
  try {
    const saved = JSON.parse(localStorage.getItem("mf35xAlarmLimits"));
    return { ...DEFAULT_LIMITS, ...(saved || {}) };
  } catch {
    return { ...DEFAULT_LIMITS };
  }
}
