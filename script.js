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
let history = { rpm: [], oilPressure: [], oilTemp: [] };
const HISTORY_MAX = 60;

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
  setConnection(data);

  setText("speed", data.speed_kmh !== undefined ? Number(data.speed_kmh).toFixed(1) : "---");
  setText("sat", data.satellites !== undefined ? data.satellites : "---");

  const now = new Date();
  document.getElementById("lastUpdateSmall").innerText = now.toLocaleTimeString("de-AT");

  const battery = readNumber(data.battery_v);
  const rpm = readNumber(data.rpm);
  const oilPressure = readNumber(data.oil_pressure);
  const oilTemp = readNumber(data.oil_temp);
  const cylTemp = readNumber(data.cylinder_temp);
  const hdop = readNumber(data.hdop);

  setText("battery", battery !== null ? battery.toFixed(1) : "---");
  setText("rpm", rpm !== null ? Math.round(rpm) : "---");
  setText("oilpressure", oilPressure !== null ? oilPressure.toFixed(1) : "---");
  setText("oiltemp", oilTemp !== null ? Math.round(oilTemp) : "---");
  setText("cyltemp", cylTemp !== null ? Math.round(cylTemp) : "---");
  setGpsQuality(hdop);

  const alarms = [];
  applyAlarmState("battery", "batteryIcon", battery, "low", limits.batteryWarn, limits.batteryAlarm, "Batteriespannung", "V", alarms);
  applyAlarmState("oilpressure", "oilpressureIcon", oilPressure, "low", limits.oilPressureWarn, limits.oilPressureAlarm, "Öldruck", "bar", alarms);
  applyAlarmState("oiltemp", "oiltempIcon", oilTemp, "high", limits.oilTempWarn, limits.oilTempAlarm, "Öltemperatur", "°C", alarms);
  applyAlarmState("cyltemp", "cyltempIcon", cylTemp, "high", limits.cylTempWarn, limits.cylTempAlarm, "Zylindertemperatur", "°C", alarms);
  updateAlarmBanner(alarms);

  addHistory("rpm", rpm);
  addHistory("oilPressure", oilPressure);
  addHistory("oilTemp", oilTemp);
  drawChart("rpmChart", history.rpm, "U/min");
  drawChart("oilPressureChart", history.oilPressure, "bar");
  drawChart("oilTempChart", history.oilTemp, "°C");

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

function applyAlarmState(valueId, iconId, value, direction, warnLimit, alarmLimit, label, unit, alarms) {
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
    alarms.push({ level: "alarm", text: `${label} kritisch: ${formatValue(value)} ${unit}` });
  } else if (warning) {
    card.classList.add("warning-card");
    valueElement.classList.add("warn-color");
    iconElement.classList.add("warn-color");
    alarms.push({ level: "warning", text: `${label} Warnung: ${formatValue(value)} ${unit}` });
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
