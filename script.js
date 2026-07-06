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
let history = { oilTemp: [], cylTemp: [] };
let maxValues = loadMaxValues();
let alarmHistory = loadAlarmHistory();
let activeAlarmKeys = new Set();
const HISTORY_MAX = 60;
const ALARM_HISTORY_MAX = 30;

let map = L.map("map").setView([48.2, 16.3], 15);
L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", {
  maxZoom: 19,
  attribution: "&copy; OpenStreetMap"
}).addTo(map);

const tractorIcon = L.icon({
  iconUrl: "tractor.png",
  iconSize: [76, 76],
  iconAnchor: [38, 38],
  popupAnchor: [0, -32],
  className: "leaflet-custom-tractor"
});

let marker = L.marker([48.2, 16.3], { icon: tractorIcon }).addTo(map);
let firstPosition = true;
let lastDataTime = 0;
let lastValidPosition = null;

setupSettingsUi();
setupMaxUi();
renderMaxValues();
renderAlarmHistory();

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
  setConnection(data);

  const speed = readNumber(data.speed_kmh);
  const battery = readNumber(data.battery_v);
  const rpm = readNumber(data.rpm);
  const oilPressure = readNumber(data.oil_pressure);
  const oilTemp = readNumber(data.oil_temp);
  const cylTemp = readNumber(data.cylinder_temp);
  const hdop = readNumber(data.hdop);

  setText("speed", speed !== null ? speed.toFixed(1) : "---");
  setText("sat", data.satellites !== undefined ? data.satellites : "---");

  const now = new Date();
  document.getElementById("lastUpdateSmall").innerText = now.toLocaleTimeString("de-AT");

  setText("battery", battery !== null ? battery.toFixed(1) : "---");
  setText("rpm", rpm !== null ? Math.round(rpm) : "---");
  setText("oilpressure", oilPressure !== null ? oilPressure.toFixed(1) : "---");
  setText("oiltemp", oilTemp !== null ? Math.round(oilTemp) : "---");
  setText("cyltemp", cylTemp !== null ? Math.round(cylTemp) : "---");
  setGpsQuality(hdop);

  updateMaxValues({ speed, rpm, oilTemp, cylTemp, oilPressure, battery });

  const alarms = [];
  applyAlarmState("battery", "batteryIcon", battery, "low", limits.batteryWarn, limits.batteryAlarm, "Batteriespannung", "V", "battery", alarms);
  applyAlarmState("oilpressure", "oilpressureIcon", oilPressure, "low", limits.oilPressureWarn, limits.oilPressureAlarm, "Öldruck", "bar", "oilPressure", alarms);
  applyAlarmState("oiltemp", "oiltempIcon", oilTemp, "high", limits.oilTempWarn, limits.oilTempAlarm, "Öltemperatur", "°C", "oilTemp", alarms);
  applyAlarmState("cyltemp", "cyltempIcon", cylTemp, "high", limits.cylTempWarn, limits.cylTempAlarm, "Zylindertemperatur", "°C", "cylTemp", alarms);
  updateAlarmBanner(alarms);
  updateAlarmHistory(alarms);

  addHistory("oilTemp", oilTemp);
  addHistory("cylTemp", cylTemp);
  drawChart("oilTempChart", history.oilTemp, "°C");
  drawChart("cylTempChart", history.cylTemp, "°C");

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

function setText(id, value) { document.getElementById(id).innerText = value; }

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
  status.classList.add(isOnline ? "online" : "offline");
  icon.classList.add(isOnline ? "online" : "offline");
}

function setConnection(data) {
  const rssi = readNumber(data.wifi_rssi);
  setText("wifiRssi", rssi !== null ? Math.round(rssi) : "---");

  if (rssi === null) {
    setText("connection", "Online");
    return;
  }

  if (rssi > -60) setText("connection", "Sehr gut");
  else if (rssi > -75) setText("connection", "Gut");
  else setText("connection", "Schwach");
}

function setGpsQuality(hdop) {
  const icon = document.getElementById("gpsQualityIcon");
  icon.classList.remove("green", "yellow", "red", "purple");

  if (hdop === null) {
    setText("gpsQuality", "---");
    setText("hdop", "---");
    icon.classList.add("purple");
    return;
  }

  setText("hdop", hdop.toFixed(1));

  if (hdop <= 1.5) {
    setText("gpsQuality", "Sehr gut");
    icon.classList.add("green");
  } else if (hdop <= 3.0) {
    setText("gpsQuality", "Mittel");
    icon.classList.add("yellow");
  } else {
    setText("gpsQuality", "Schlecht");
    icon.classList.add("red");
  }
}

function setOfflineValues(statusText) {
  setStatus(statusText, false);
  setText("connection", "Offline");
  setText("wifiRssi", "---");

  ["speed", "sat", "battery", "rpm", "oilpressure", "oiltemp", "cyltemp", "gpsQuality", "hdop"].forEach(id => setText(id, "---"));
  ["battery", "oilpressure", "oiltemp", "cyltemp"].forEach(clearAlarmState);
  updateAlarmBanner([]);

  if (lastDataTime === 0) document.getElementById("lastUpdateSmall").innerText = "---";

  const mapsButton = document.getElementById("mapsButton");
  if (lastValidPosition) {
    mapsButton.href = "https://www.google.com/maps?q=" + lastValidPosition.lat + "," + lastValidPosition.lng;
    mapsButton.classList.remove("disabled");
  } else {
    mapsButton.href = "#";
    mapsButton.classList.add("disabled");
  }
}

function applyAlarmState(valueId, iconId, value, direction, warnLimit, alarmLimit, label, unit, alarmKey, alarms) {
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
    alarms.push({ key: alarmKey + "_alarm", level: "alarm", text: `${label} kritisch: ${formatValue(value)} ${unit}` });
  } else if (warning) {
    card.classList.add("warning-card");
    valueElement.classList.add("warn-color");
    iconElement.classList.add("warn-color");
    alarms.push({ key: alarmKey + "_warning", level: "warning", text: `${label} Warnung: ${formatValue(value)} ${unit}` });
  }
}

function formatValue(value) {
  return Number.isInteger(value) ? value : value.toFixed(1);
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

function updateAlarmBanner(alarms) {
  const banner = document.getElementById("alarmBanner");
  const text = document.getElementById("alarmText");

  if (!alarms.length) {
    banner.classList.add("hidden");
    return;
  }

  const hasAlarm = alarms.some(a => a.level === "alarm");
  banner.classList.remove("hidden", "warning");
  if (!hasAlarm) banner.classList.add("warning");

  text.innerText = alarms.map(a => a.text).join(" · ");
}

function updateAlarmHistory(alarms) {
  const currentKeys = new Set(alarms.map(a => a.key));

  alarms.forEach(alarm => {
    if (!activeAlarmKeys.has(alarm.key)) {
      alarmHistory.unshift({
        time: new Date().toLocaleTimeString("de-AT"),
        level: alarm.level,
        text: alarm.text
      });
    }
  });

  activeAlarmKeys = currentKeys;
  if (alarmHistory.length > ALARM_HISTORY_MAX) alarmHistory = alarmHistory.slice(0, ALARM_HISTORY_MAX);

  localStorage.setItem("mf35xAlarmHistory", JSON.stringify(alarmHistory));
  renderAlarmHistory();
}

function renderAlarmHistory() {
  const container = document.getElementById("alarmHistory");

  if (!alarmHistory.length) {
    container.innerHTML = '<div class="empty-history">Noch keine Alarme.</div>';
    return;
  }

  container.innerHTML = alarmHistory.map(entry => `
    <div class="alarm-entry ${entry.level === "warning" ? "warning-entry" : ""}">
      <div class="alarm-time">${entry.time}</div>
      <div class="alarm-message">${entry.text}</div>
    </div>
  `).join("");
}

function loadAlarmHistory() {
  try {
    const saved = JSON.parse(localStorage.getItem("mf35xAlarmHistory"));
    return Array.isArray(saved) ? saved : [];
  } catch {
    return [];
  }
}

function updateMaxValues(values) {
  if (values.speed !== null) maxValues.maxSpeed = maxOrValue(maxValues.maxSpeed, values.speed);
  if (values.rpm !== null) maxValues.maxRpm = maxOrValue(maxValues.maxRpm, values.rpm);
  if (values.oilTemp !== null) maxValues.maxOilTemp = maxOrValue(maxValues.maxOilTemp, values.oilTemp);
  if (values.cylTemp !== null) maxValues.maxCylTemp = maxOrValue(maxValues.maxCylTemp, values.cylTemp);
  if (values.oilPressure !== null) maxValues.minOilPressure = minOrValue(maxValues.minOilPressure, values.oilPressure);
  if (values.battery !== null) maxValues.minBattery = minOrValue(maxValues.minBattery, values.battery);

  localStorage.setItem("mf35xMaxValues", JSON.stringify(maxValues));
  renderMaxValues();
}

function maxOrValue(current, value) {
  return current === null || current === undefined ? value : Math.max(current, value);
}

function minOrValue(current, value) {
  return current === null || current === undefined ? value : Math.min(current, value);
}

function renderMaxValues() {
  setText("maxSpeed", maxValues.maxSpeed != null ? maxValues.maxSpeed.toFixed(1) : "---");
  setText("maxRpm", maxValues.maxRpm != null ? Math.round(maxValues.maxRpm) : "---");
  setText("maxOilTemp", maxValues.maxOilTemp != null ? Math.round(maxValues.maxOilTemp) : "---");
  setText("maxCylTemp", maxValues.maxCylTemp != null ? Math.round(maxValues.maxCylTemp) : "---");
  setText("minOilPressure", maxValues.minOilPressure != null ? maxValues.minOilPressure.toFixed(1) : "---");
  setText("minBattery", maxValues.minBattery != null ? maxValues.minBattery.toFixed(1) : "---");
}

function loadMaxValues() {
  try {
    const saved = JSON.parse(localStorage.getItem("mf35xMaxValues"));
    return {
      maxSpeed: saved?.maxSpeed ?? null,
      maxRpm: saved?.maxRpm ?? null,
      maxOilTemp: saved?.maxOilTemp ?? null,
      maxCylTemp: saved?.maxCylTemp ?? null,
      minOilPressure: saved?.minOilPressure ?? null,
      minBattery: saved?.minBattery ?? null
    };
  } catch {
    return {
      maxSpeed: null,
      maxRpm: null,
      maxOilTemp: null,
      maxCylTemp: null,
      minOilPressure: null,
      minBattery: null
    };
  }
}

function setupMaxUi() {
  document.getElementById("resetMaxValues").addEventListener("click", () => {
    maxValues = {
      maxSpeed: null,
      maxRpm: null,
      maxOilTemp: null,
      maxCylTemp: null,
      minOilPressure: null,
      minBattery: null
    };
    localStorage.setItem("mf35xMaxValues", JSON.stringify(maxValues));
    renderMaxValues();
  });

  document.getElementById("clearAlarmHistory").addEventListener("click", () => {
    alarmHistory = [];
    activeAlarmKeys = new Set();
    localStorage.setItem("mf35xAlarmHistory", JSON.stringify(alarmHistory));
    renderAlarmHistory();
  });
}

function addHistory(key, value) {
  if (value === null) return;
  history[key].push(value);
  if (history[key].length > HISTORY_MAX) history[key].shift();
}

function drawChart(canvasId, values, unit) {
  const canvas = document.getElementById(canvasId);
  const ctx = canvas.getContext("2d");
  const width = canvas.width;
  const height = canvas.height;

  ctx.clearRect(0, 0, width, height);
  ctx.strokeStyle = "#444";
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(35, 10);
  ctx.lineTo(35, height - 25);
  ctx.lineTo(width - 10, height - 25);
  ctx.stroke();

  if (values.length < 2) {
    ctx.fillStyle = "#aaa";
    ctx.font = "14px Arial";
    ctx.fillText("Warte auf Daten...", 45, 80);
    return;
  }

  const min = Math.min(...values);
  const max = Math.max(...values);
  const range = max - min || 1;

  ctx.fillStyle = "#aaa";
  ctx.font = "12px Arial";
  ctx.fillText(`${formatValue(max)} ${unit}`, 38, 20);
  ctx.fillText(`${formatValue(min)} ${unit}`, 38, height - 30);

  ctx.strokeStyle = "#2e9bff";
  ctx.lineWidth = 3;
  ctx.beginPath();

  values.forEach((v, i) => {
    const x = 35 + (i / (HISTORY_MAX - 1)) * (width - 50);
    const y = 10 + ((max - v) / range) * (height - 40);
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });

  ctx.stroke();
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
