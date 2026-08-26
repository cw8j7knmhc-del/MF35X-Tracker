/* MF35X Tracker V9.5.12 – Besucher liest Maximalwerte/Alarmhistorie nur noch */
import { initializeApp } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js";
import { getDatabase, ref, onValue } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-database.js";
import { firebaseConfig } from "./firebase-config.js";

const app = initializeApp(firebaseConfig);
const db = getDatabase(app);

const DEF = {
  batteryWarn: 12.2,
  batteryAlarm: 11.8,
  oilPressureWarn: 2,
  oilPressureAlarm: 1.2,
  oilTempWarn: 110,
  oilTempAlarm: 125,
  cylTempWarn: 180,
  cylTempAlarm: 220
};

let limits = DEF;
let h = { oilTemp: [], cylTemp: [] };
let active = new Set();
let last = 0;
let lastPos = null;
let first = true;
let currentLive = null;
let settingsReady = false;
let oilPressureEngineStartAt = 0;

/*
 * Offline-Erkennung:
 * - mindestens 5 Sekunden Reserve, damit kurze Firebase-/Browser-Verzögerungen
 *   nicht mehr als Verbindungsabbruch erscheinen
 * - bei langsameren Uploadintervallen automatisch 3 x Uploadintervall
 * - maximal 15 Sekunden
 */
const MIN_LIVE_TIMEOUT_MS = 5000;
const MAX_LIVE_TIMEOUT_MS = 15000;
const LIVE_TIMEOUT_FACTOR = 3;
let liveTimeoutMs = MIN_LIVE_TIMEOUT_MS;

const HMAX = 60;
const OIL_PRESSURE_RPM_MIN = 400;
const OIL_PRESSURE_START_DELAY_MS = 5000;

let map = L.map("map").setView([48.2, 16.3], 15);
L.tileLayer(
  "https://services.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}",
  {
    maxZoom: 19,
    attribution:
      "Tiles &copy; Esri &mdash; Source: Esri, Vantor, Earthstar Geographics, and the GIS User Community"
  }
).addTo(map);

// Der Marker wird erst eingeblendet, wenn mindestens eine echte GPS-Position
// vorhanden ist. Dadurch erscheint beim ersten Start keine erfundene Position.
let marker = L.marker([48.2, 16.3], {
  icon: L.icon({
    iconUrl: "tractor.png",
    iconSize: [76, 76],
    iconAnchor: [38, 38],
    className: "leaflet-custom-tractor"
  })
});

setupNotifications();

onValue(ref(db, "tracker/device"), s => {
  let d = s.val() || {};
  let u = num(d.uploadIntervalMs);

  if (u != null) {
    liveTimeoutMs = Math.max(
      MIN_LIVE_TIMEOUT_MS,
      Math.min(MAX_LIVE_TIMEOUT_MS, u * LIVE_TIMEOUT_FACTOR)
    );
  }
});

onValue(ref(db, "tracker/settings"), s => {
  limits = { ...DEF, ...(s.val() || {}) };
  settingsReady = true;
  evaluateAlarms();
});

onValue(ref(db, "tracker/maxValues"), s => renderMax(s.val() || {}));
onValue(ref(db, "tracker/alarmHistory"), s => renderHist(s.val() || []));

onValue(ref(db, "tracker/live"), s => {
  let d = s.val();

  if (!d) {
    currentLive = null;
    last = 0;
    offline("Keine Daten");
    return;
  }

  const gpsValid = d.gps_valid === true;
  const lat = num(d.lat);
  const lng = num(d.lng);
  const positionAvailable =
    lat != null && lng != null &&
    lat >= -90 && lat <= 90 &&
    lng >= -180 && lng <= 180;

  // Letzte bekannte Position immer zuerst uebernehmen.
  // So bleibt sie auch sichtbar, wenn der ESP32 beim Oeffnen bereits offline ist.
  if (positionAvailable) {
    lastPos = { lat, lng };

    if (!map.hasLayer(marker)) {
      marker.addTo(map);
    }

    marker.setLatLng([lat, lng]);

    if (first) {
      map.setView([lat, lng], 17);
      first = false;
    }
  }

  let ts = num(d.timestamp);

  if (ts == null) {
    currentLive = null;
    last = 0;
    offline("Zeitstempel fehlt");
    return;
  }

  last = ts;

  if (Date.now() - ts > liveTimeoutMs) {
    currentLive = null;
    txt("lastUpdateSmall", new Date(ts).toLocaleTimeString("de-AT"));
    offline("Offline");
    return;
  }

  // Frischer Firebase-Zeitstempel bedeutet: ESP32 ist online.
  // Ein fehlender GPS-Fix darf diesen Online-Status NICHT mehr überschreiben.
  currentLive = d;
  status("Online", 1);
  conn(d);

  let speed = num(d.speed_kmh),
    bat = num(d.battery_v),
    rpm = num(d.rpm),
    op = num(d.oil_pressure),
    ot = num(d.oil_temp),
    ct = num(d.cylinder_temp),
    hd = num(d.hdop);

  txt("speed", speed != null ? speed.toFixed(1) : "---");
  txt("sat", d.satellites ?? "---");
  txt("lastUpdateSmall", new Date(ts).toLocaleTimeString("de-AT"));
  txt("battery", bat != null ? bat.toFixed(1) : "---");
  txt("rpm", rpm != null ? Math.round(rpm) : "---");
  txt("oilpressure", op != null ? op.toFixed(1) : "---");
  txt("oiltemp", ot != null ? Math.round(ot) : "---");
  txt("cyltemp", ct != null ? Math.round(ct) : "---");

  gpsq(hd, gpsValid);
  evaluateAlarms();

  add("oilTemp", ot);
  add("cylTemp", ct);
  chart("oilTempChart", h.oilTemp, "°C");
  chart("cylTempChart", h.cylTemp, "°C");

  // Im Online-Betrieb folgt die Karte weiterhin neuen gueltigen GPS-Positionen.
  if (positionAvailable && gpsValid) {
    map.panTo([lat, lng]);
  }

  updateMapsButton(gpsValid);
});

/* Prüft viermal pro Sekunde, ob die letzten Daten wirklich zu alt sind. */
setInterval(() => {
  if (!last) {
    offline("Keine Daten");
    return;
  }

  if (Date.now() - last > liveTimeoutMs) {
    currentLive = null;
    offline("Offline");
    return;
  }

  status("Online", 1);
}, 250);

function txt(i, v) {
  document.getElementById(i).innerText = v;
}

function num(v) {
  if (v === undefined || v === null || v === "") return null;
  v = Number(v);
  return isNaN(v) ? null : v;
}

function status(t, on) {
  let s = document.getElementById("status");
  let i = document.getElementById("statusIcon");

  s.innerText = t;
  s.className = "value " + (on ? "online" : "offline");
  i.classList.remove("online", "offline");
  i.classList.add(on ? "online" : "offline");
}

function conn(d) {
  let r = num(d.wifi_rssi);
  txt("wifiRssi", r != null ? Math.round(r) : "---");
  txt(
    "connection",
    r == null ? "Online" : r > -60 ? "Sehr gut" : r > -75 ? "Gut" : "Schwach"
  );
}

function gpsq(hd, gpsValid) {
  let i = document.getElementById("gpsQualityIcon");
  i.classList.remove("green", "yellow", "red", "purple");

  if (!gpsValid) {
    txt("gpsQuality", "Kein Fix");
    txt("hdop", "---");
    i.classList.add("purple");
    return;
  }

  if (hd == null) {
    txt("gpsQuality", "---");
    txt("hdop", "---");
    i.classList.add("purple");
    return;
  }

  txt("hdop", hd.toFixed(1));
  txt("gpsQuality", hd <= 1.5 ? "Sehr gut" : hd <= 3 ? "Mittel" : "Schlecht");
  i.classList.add(hd <= 1.5 ? "green" : hd <= 3 ? "yellow" : "red");
}

function updateMapsButton(gpsValid) {
  let mb = document.getElementById("mapsButton");

  if (!lastPos) {
    mb.href = "#";
    mb.classList.add("disabled");
    mb.innerHTML = '<i class="fa-solid fa-location-dot"></i> Noch keine GPS-Position';
    return;
  }

  mb.href = `https://www.google.com/maps?q=${lastPos.lat},${lastPos.lng}`;
  mb.classList.remove("disabled");
  mb.innerHTML = gpsValid
    ? '<i class="fa-solid fa-location-dot"></i> In Google Maps öffnen'
    : '<i class="fa-solid fa-location-dot"></i> Letzte Position in Google Maps öffnen';
}

/*
 * WICHTIGER FIX:
 * Bei einem echten Timeout werden die letzten gültigen Messwerte NICHT mehr
 * sofort auf --- gesetzt. Dadurch gibt es kein sichtbares Flackern mehr.
 * Der Status zeigt trotzdem eindeutig Offline und der Zeitstempel bleibt als
 * Hinweis erhalten, wie alt die letzte Messung ist.
 */
function offline(t) {
  status(t, 0);

  const neverReceivedData = !last;

  if (neverReceivedData) {
    [
      "speed",
      "sat",
      "battery",
      "rpm",
      "oilpressure",
      "oiltemp",
      "cyltemp",
      "gpsQuality",
      "hdop",
      "connection",
      "wifiRssi"
    ].forEach(i => txt(i, "---"));

    txt("lastUpdateSmall", "---");
  } else if (t === "Offline") {
    /* Sensorwerte und letzter RSSI bleiben sichtbar; nur der Verbindungsstatus
       kennzeichnet, dass gerade keine neuen Daten eintreffen. */
    txt("connection", "Keine neuen Daten");
  }

  ["battery", "oilpressure", "oiltemp", "cyltemp"].forEach(clear);
  banner([]);
  active.clear();
  oilPressureEngineStartAt = 0;

  // Auch bei echtem ESP32-Offline bleibt die letzte bekannte Position erhalten.
  updateMapsButton(false);
}

function evaluateAlarms() {
  if (!settingsReady || !currentLive || !last) {
    ["battery", "oilpressure", "oiltemp", "cyltemp"].forEach(clear);
    banner([]);
    active.clear();
    return;
  }

  let bat = num(currentLive.battery_v),
    rpm = num(currentLive.rpm),
    op = num(currentLive.oil_pressure),
    ot = num(currentLive.oil_temp),
    ct = num(currentLive.cylinder_temp),
    a = [];

  alarm(
    "battery",
    "batteryIcon",
    bat,
    "low",
    limits.batteryWarn,
    limits.batteryAlarm,
    "Batteriespannung",
    "V",
    "battery",
    a
  );

  const engineRunning = rpm != null && rpm >= OIL_PRESSURE_RPM_MIN;

  if (!engineRunning) {
    oilPressureEngineStartAt = 0;
    clear("oilpressure");
  } else {
    if (!oilPressureEngineStartAt) {
      oilPressureEngineStartAt = Date.now();
    }

    const startDelayFinished =
      Date.now() - oilPressureEngineStartAt >= OIL_PRESSURE_START_DELAY_MS;

    if (startDelayFinished) {
      alarm(
        "oilpressure",
        "oilpressureIcon",
        op,
        "low",
        limits.oilPressureWarn,
        limits.oilPressureAlarm,
        "Öldruck",
        "bar",
        "oilPressure",
        a
      );
    } else {
      clear("oilpressure");
    }
  }

  alarm(
    "oiltemp",
    "oiltempIcon",
    ot,
    "high",
    limits.oilTempWarn,
    limits.oilTempAlarm,
    "Öltemperatur",
    "°C",
    "oilTemp",
    a
  );

  alarm(
    "cyltemp",
    "cyltempIcon",
    ct,
    "high",
    limits.cylTempWarn,
    limits.cylTempAlarm,
    "Zylindertemperatur",
    "°C",
    "cylTemp",
    a
  );

  banner(a);
  hist(a);
}

function alarm(vid, iid, v, dir, w, a, label, unit, key, out) {
  let ve = document.getElementById(vid);
  let ie = document.getElementById(iid);
  let c = ve.closest(".card");

  clear(vid);
  if (v == null) return;

  let al = dir == "low" ? v <= a : v >= a;
  let wa = dir == "low" ? v <= w && !al : v >= w && !al;

  if (al) {
    c.classList.add("alarm-card");
    ve.classList.add("alarm-color");
    ie.classList.add("alarm-color");
    out.push({
      key: key + "_alarm",
      level: "alarm",
      text: `${label} kritisch: ${fmt(v)} ${unit}`
    });
  } else if (wa) {
    c.classList.add("warning-card");
    ve.classList.add("warn-color");
    ie.classList.add("warn-color");
    out.push({
      key: key + "_warning",
      level: "warning",
      text: `${label} Warnung: ${fmt(v)} ${unit}`
    });
  }
}

function clear(id) {
  let ve = document.getElementById(id);
  if (!ve) return;

  let c = ve.closest(".card");
  let ic = c.querySelector(".icon");

  c.classList.remove("warning-card", "alarm-card");
  ve.classList.remove("warn-color", "alarm-color");
  ic && ic.classList.remove("warn-color", "alarm-color");
}

function banner(a) {
  let b = document.getElementById("alarmBanner");
  let t = document.getElementById("alarmText");

  if (!a.length) {
    b.classList.add("hidden");
    return;
  }

  b.classList.remove("hidden", "warning");
  if (!a.some(x => x.level == "alarm")) b.classList.add("warning");
  t.innerText = a.map(x => x.text).join(" · ");
}







function hist(a) {
  const cur = new Set(a.map(x => x.key));
  const neu = a.filter(x => !active.has(x.key));
  active = cur;

  // Historie selbst wird ab V5.9.13 ausschliesslich vom ESP32 geschrieben.
  // Der Browser darf weiterhin lokale Benachrichtigungen anzeigen.
  neu.forEach(x => notify("MF35X Alarm", x.text));
}


function renderHist(raw) {
  const c = document.getElementById("alarmHistory");
  const ar = normalizeAlarmHistory(raw);

  if (!ar.length) {
    c.innerHTML = '<div class="empty-history">Noch keine Alarme.</div>';
    return;
  }

  c.innerHTML = ar
    .map(
      e =>
        `<div class="alarm-entry ${
          e.level == "warning" ? "warning-entry" : ""
        }"><div class="alarm-time">${formatAlarmHistoryTime(e)}</div><div class="alarm-message">${
          e.text || "Alarm"
        }</div></div>`
    )
    .join("");
}

function normalizeAlarmHistory(raw) {
  let ar = [];
  if (Array.isArray(raw)) {
    ar = raw.filter(Boolean);
  } else if (raw && typeof raw === "object") {
    ar = Object.values(raw).filter(x => x && typeof x === "object");
  }

  return ar
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

function formatAlarmHistoryTime(entry) {
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


function renderMax(m) {
  txt("maxSpeed", m.maxSpeed != null ? Number(m.maxSpeed).toFixed(1) : "---");
  txt("maxRpm", m.maxRpm != null ? Math.round(m.maxRpm) : "---");
  txt("maxOilTemp", m.maxOilTemp != null ? Math.round(m.maxOilTemp) : "---");
  txt("maxCylTemp", m.maxCylTemp != null ? Math.round(m.maxCylTemp) : "---");
  txt(
    "minOilPressure",
    m.minOilPressure != null ? Number(m.minOilPressure).toFixed(1) : "---"
  );
  txt("minBattery", m.minBattery != null ? Number(m.minBattery).toFixed(1) : "---");
}







function fmt(v) {
  return Number.isInteger(v) ? v : v.toFixed(1);
}

function add(k, v) {
  if (v == null) return;
  h[k].push(v);
  if (h[k].length > HMAX) h[k].shift();
}

function chart(id, val, unit) {
  let ca = document.getElementById(id);
  let x = ca.getContext("2d");
  let w = ca.width;
  let hh = ca.height;

  x.clearRect(0, 0, w, hh);
  x.strokeStyle = "#444";
  x.beginPath();
  x.moveTo(35, 10);
  x.lineTo(35, hh - 25);
  x.lineTo(w - 10, hh - 25);
  x.stroke();

  if (val.length < 2) {
    x.fillStyle = "#aaa";
    x.font = "14px Arial";
    x.fillText("Warte auf Daten...", 45, 80);
    return;
  }

  let mn = Math.min(...val);
  let mx = Math.max(...val);
  let r = mx - mn || 1;

  x.strokeStyle = "#2e9bff";
  x.lineWidth = 3;
  x.beginPath();

  val.forEach((v, i) => {
    let xx = 35 + (i / (HMAX - 1)) * (w - 50);
    let yy = 10 + ((mx - v) / r) * (hh - 40);
    i ? x.lineTo(xx, yy) : x.moveTo(xx, yy);
  });

  x.stroke();
}

function setupNotifications() {
  let b = document.getElementById("requestNotifications");
  let tog = document.getElementById("notificationToggle");
  let s = document.getElementById("notifyStatus");

  tog.checked = localStorage.getItem("mf35xNotificationsEnabled") === "true";
  up();

  if (!("Notification" in window)) {
    s.innerText = "Nicht unterstützt";
    b.disabled = true;
    tog.disabled = true;
    return;
  }

  b.onclick = async () => {
    let p = await Notification.requestPermission();
    tog.checked = p === "granted";
    localStorage.setItem(
      "mf35xNotificationsEnabled",
      tog.checked ? "true" : "false"
    );
    up();

    if (tog.checked) {
      notify("MF35X Tracker", "Benachrichtigungen sind aktiviert.");
    }
  };

  tog.onchange = async () => {
    if (tog.checked && Notification.permission !== "granted") {
      let p = await Notification.requestPermission();
      if (p !== "granted") tog.checked = false;
    }

    localStorage.setItem(
      "mf35xNotificationsEnabled",
      tog.checked ? "true" : "false"
    );
    up();
  };

  function up() {
    if (!("Notification" in window)) {
      s.innerText = "Nicht unterstützt";
      return;
    }

    s.innerText =
      localStorage.getItem("mf35xNotificationsEnabled") === "true" ? "Ein" : "Aus";
    b.style.display = Notification.permission === "granted" ? "none" : "inline-block";
  }
}

function notify(title, body) {
  if (
    "Notification" in window &&
    Notification.permission === "granted" &&
    localStorage.getItem("mf35xNotificationsEnabled") === "true"
  ) {
    new Notification(title, { body, icon: "tractor.png" });
  }
}
