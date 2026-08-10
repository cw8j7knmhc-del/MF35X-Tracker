/* MF35X Tracker Rennauswertung V9.5.0 */
import { initializeApp } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js";
import {
  getDatabase, ref, get, query, orderByChild, startAt, endAt
} from "https://www.gstatic.com/firebasejs/10.12.2/firebase-database.js";
import { firebaseConfig } from "./firebase-config.js";

const app = initializeApp(firebaseConfig);
const db = getDatabase(app);

const METRICS = {
  cylinder_temp: { label: "Zylinderkopf", unit: "°C", color: "#ff4040", group: "temperature", decimals: 0 },
  oil_temp: { label: "Motoröl", unit: "°C", color: "#ff951f", group: "temperature", decimals: 0 },
  gear_oil_temp: { label: "Getriebeöl", unit: "°C", color: "#ffd24b", group: "temperature", decimals: 0 },
  oil_pressure: { label: "Öldruck", unit: "bar", color: "#2e9bff", group: "operating", decimals: 1, axis: "yPressure" },
  rpm: { label: "Drehzahl", unit: "U/min", color: "#a04cff", group: "operating", decimals: 0, axis: "yRpm" },
  speed_kmh: { label: "Geschwindigkeit", unit: "km/h", color: "#43ff5f", group: "operating", decimals: 1, axis: "ySpeed" }
};

let races = {};
let currentSamples = [];
let currentRaceId = "";
let settings = {};
let temperatureChart = null;
let operatingChart = null;

const raceSelect = document.getElementById("raceSelect");
const fromTime = document.getElementById("fromTime");
const toTime = document.getElementById("toTime");
const loadRangeButton = document.getElementById("loadRange");
const loadFullButton = document.getElementById("loadFullRace");
const exportButton = document.getElementById("exportCsv");

raceSelect.addEventListener("change", onRaceChange);
loadRangeButton.addEventListener("click", loadSelectedRange);
loadFullButton.addEventListener("click", loadFullRace);
exportButton.addEventListener("click", exportCsv);
document.querySelectorAll(".metric-toggle").forEach(cb => cb.addEventListener("change", renderCharts));

await loadSettings();
await loadRaces();

async function loadSettings() {
  try {
    const snapshot = await get(ref(db, "tracker/settings"));
    settings = snapshot.val() || {};
  } catch (error) {
    console.warn("Alarmgrenzen konnten nicht geladen werden:", error);
  }
}

async function loadRaces() {
  setStatus("Lade Rennen…", "pending");

  try {
    const snapshot = await get(ref(db, "tracker/races"));
    races = snapshot.val() || {};

    const entries = Object.entries(races)
      .sort((a, b) => Number(b[1]?.startedAt || 0) - Number(a[1]?.startedAt || 0));

    raceSelect.innerHTML = "";

    if (!entries.length) {
      raceSelect.innerHTML = '<option value="">Noch keine Rennaufzeichnung vorhanden</option>';
      raceSelect.disabled = true;
      loadRangeButton.disabled = true;
      loadFullButton.disabled = true;
      setStatus("Noch keine Rennen in Firebase.", "pending");
      return;
    }

    for (const [id, meta] of entries) {
      const option = document.createElement("option");
      option.value = id;
      option.textContent = `${meta.name || id} · ${formatDateTime(meta.startedAt)}`;
      raceSelect.appendChild(option);
    }

    raceSelect.disabled = false;
    loadRangeButton.disabled = false;
    loadFullButton.disabled = false;
    setStatus(`${entries.length} Rennen gefunden.`, "success");

    raceSelect.value = entries[0][0];
    onRaceChange();
  } catch (error) {
    setStatus("Firebase-Lesefehler: " + error.message, "error");
  }
}

function onRaceChange() {
  currentRaceId = raceSelect.value;
  currentSamples = [];
  exportButton.disabled = true;
  renderCharts();
  renderStats();

  const meta = races[currentRaceId];

  if (!meta) {
    document.getElementById("raceInfo").textContent = "Noch kein Rennen ausgewählt.";
    return;
  }

  const start = Number(meta.startedAt || Date.now());
  const stop = Number(meta.stoppedAt || Date.now());

  fromTime.value = toLocalInputValue(start);
  toTime.value = toLocalInputValue(stop);

  document.getElementById("raceInfo").innerHTML =
    `<strong>${escapeHtml(meta.name || currentRaceId)}</strong> · ` +
    `Start: ${formatDateTime(start)} · ` +
    `${meta.stoppedAt ? "Ende: " + formatDateTime(stop) : "Aufzeichnung läuft"} · ` +
    `Archivintervall: ${Number(meta.history_update_ms || 5000) / 1000} s`;
}

async function loadFullRace() {
  const meta = races[raceSelect.value];
  if (!meta) return;

  const start = Number(meta.startedAt || 0);
  const stop = Number(meta.stoppedAt || Date.now());

  fromTime.value = toLocalInputValue(start);
  toTime.value = toLocalInputValue(stop);
  await loadRange(start, stop);
}

async function loadSelectedRange() {
  const start = new Date(fromTime.value).getTime();
  const stop = new Date(toTime.value).getTime();

  if (!Number.isFinite(start) || !Number.isFinite(stop)) {
    setStatus("Bitte gültigen Start- und Endzeitpunkt eingeben.", "error");
    return;
  }

  if (stop <= start) {
    setStatus("Der Endzeitpunkt muss nach dem Start liegen.", "error");
    return;
  }

  await loadRange(start, stop);
}

async function loadRange(start, stop) {
  const raceId = raceSelect.value;
  if (!raceId) return;

  setStatus("Historische Daten werden geladen…", "pending");
  loadRangeButton.disabled = true;
  loadFullButton.disabled = true;
  exportButton.disabled = true;

  try {
    const historyQuery = query(
      ref(db, `tracker/history/${raceId}`),
      orderByChild("timestamp"),
      startAt(start),
      endAt(stop)
    );

    const snapshot = await get(historyQuery);
    const raw = snapshot.val() || {};

    currentSamples = Object.values(raw)
      .filter(sample => sample && Number.isFinite(Number(sample.timestamp)))
      .map(sample => normalizeSample(sample))
      .sort((a, b) => a.timestamp - b.timestamp);

    document.getElementById("sampleCount").textContent =
      `${currentSamples.length.toLocaleString("de-AT")} Datensätze`;

    if (!currentSamples.length) {
      setStatus("Für diesen Zeitraum sind noch keine historischen Daten gespeichert.", "pending");
      renderCharts();
      renderStats();
      return;
    }

    setStatus(
      `${currentSamples.length.toLocaleString("de-AT")} Datensätze geladen · ` +
      `${formatDateTime(currentSamples[0].timestamp)} bis ${formatDateTime(currentSamples.at(-1).timestamp)}`,
      "success"
    );

    exportButton.disabled = false;
    renderCharts();
    renderStats();
  } catch (error) {
    setStatus("Historie konnte nicht geladen werden: " + error.message, "error");
  } finally {
    loadRangeButton.disabled = false;
    loadFullButton.disabled = false;
  }
}

function normalizeSample(sample) {
  const normalized = { timestamp: Number(sample.timestamp) };
  for (const key of Object.keys(METRICS)) {
    const n = Number(sample[key]);
    normalized[key] = Number.isFinite(n) ? n : null;
  }
  return normalized;
}

function selectedMetrics(group) {
  return [...document.querySelectorAll(".metric-toggle:checked")]
    .map(cb => cb.value)
    .filter(key => METRICS[key]?.group === group);
}

function renderCharts() {
  renderTemperatureChart();
  renderOperatingChart();
}

function renderTemperatureChart() {
  const selected = selectedMetrics("temperature");
  const datasets = selected.map(key => {
    const metric = METRICS[key];
    return {
      label: `${metric.label} (${metric.unit})`,
      data: currentSamples
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

  if (temperatureChart) temperatureChart.destroy();

  temperatureChart = new Chart(document.getElementById("temperatureHistoryChart"), {
    type: "line",
    data: { datasets },
    options: baseChartOptions("Temperatur (°C)", {
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

function renderOperatingChart() {
  const selected = selectedMetrics("operating");
  const datasets = selected.map(key => {
    const metric = METRICS[key];
    return {
      label: `${metric.label} (${metric.unit})`,
      data: currentSamples
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

  if (operatingChart) operatingChart.destroy();

  operatingChart = new Chart(document.getElementById("operatingHistoryChart"), {
    type: "line",
    data: { datasets },
    options: baseChartOptions("", {
      yPressure: axisOptions("Öldruck (bar)", "left", "#2e9bff"),
      yRpm: axisOptions("Drehzahl (U/min)", "right", "#a04cff"),
      ySpeed: axisOptions("Geschwindigkeit (km/h)", "right", "#43ff5f")
    })
  });
}

function baseChartOptions(yTitle, yScales) {
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
            return formatDateTime(items[0].parsed.x, true);
          }
        }
      },
      decimation: {
        enabled: true,
        algorithm: "lttb",
        samples: 1600
      }
    },
    scales: {
      x: {
        type: "linear",
        ticks: {
          color: "#aaa",
          maxTicksLimit: 10,
          callback(value) { return formatAxisTime(value); }
        },
        grid: { color: "#2a2a2a" },
        title: { display: true, text: "Zeit", color: "#bbb" }
      },
      ...yScales
    }
  };
}

function axisOptions(title, position, color) {
  return {
    type: "linear",
    position,
    display: "auto",
    title: { display: true, text: title, color },
    ticks: { color },
    grid: { drawOnChartArea: position === "left", color: "#333" }
  };
}

function renderStats() {
  const container = document.getElementById("statsGrid");

  if (!currentSamples.length) {
    container.innerHTML = '<div class="empty-history">Noch keine historischen Daten geladen.</div>';
    return;
  }

  container.innerHTML = Object.entries(METRICS).map(([key, metric]) => {
    const values = currentSamples
      .filter(s => s[key] != null)
      .map(s => ({ timestamp: s.timestamp, value: s[key] }));

    if (!values.length) {
      return `
        <div class="stat-card">
          <div class="stat-title">${metric.label}</div>
          <div class="stat-empty">Keine Daten</div>
        </div>`;
    }

    const rawValues = values.map(v => v.value);
    const min = Math.min(...rawValues);
    const max = Math.max(...rawValues);
    const avg = rawValues.reduce((sum, v) => sum + v, 0) / rawValues.length;
    const maxPoint = values.reduce((best, p) => p.value > best.value ? p : best);

    const thresholdText = thresholdDurationText(key, values);

    return `
      <div class="stat-card">
        <div class="stat-title">${metric.label}</div>
        <div class="stat-row"><span>Minimum</span><strong>${formatMetric(min, metric)} ${metric.unit}</strong></div>
        <div class="stat-row"><span>Maximum</span><strong>${formatMetric(max, metric)} ${metric.unit}</strong></div>
        <div class="stat-row"><span>Durchschnitt</span><strong>${formatMetric(avg, metric)} ${metric.unit}</strong></div>
        <div class="stat-row"><span>Maximum am</span><strong>${formatDateTime(maxPoint.timestamp)}</strong></div>
        ${thresholdText}
      </div>`;
  }).join("");
}

function thresholdDurationText(key, values) {
  let warn = null;
  let alarm = null;
  let direction = "high";

  if (key === "oil_temp") {
    warn = numberOrNull(settings.oilTempWarn);
    alarm = numberOrNull(settings.oilTempAlarm);
  } else if (key === "cylinder_temp") {
    warn = numberOrNull(settings.cylTempWarn);
    alarm = numberOrNull(settings.cylTempAlarm);
  } else if (key === "oil_pressure") {
    warn = numberOrNull(settings.oilPressureWarn);
    alarm = numberOrNull(settings.oilPressureAlarm);
    direction = "low";
  } else {
    return '<div class="stat-row"><span>Grenzzeit</span><strong>—</strong></div>';
  }

  if (warn == null || alarm == null) {
    return '<div class="stat-row"><span>Grenzzeit</span><strong>Grenzen fehlen</strong></div>';
  }

  const warnMs = durationBeyond(values, warn, direction);
  const alarmMs = durationBeyond(values, alarm, direction);

  return `
    <div class="stat-row"><span>Warnbereich</span><strong>${formatDuration(warnMs)}</strong></div>
    <div class="stat-row"><span>Alarmbereich</span><strong>${formatDuration(alarmMs)}</strong></div>`;
}

function durationBeyond(values, threshold, direction) {
  if (values.length < 2) return 0;

  const intervals = [];
  for (let i = 1; i < values.length; i++) {
    const dt = values[i].timestamp - values[i - 1].timestamp;
    if (dt > 0 && dt < 60000) intervals.push(dt);
  }

  const median = intervals.length
    ? intervals.sort((a, b) => a - b)[Math.floor(intervals.length / 2)]
    : 5000;

  const maxGap = Math.max(15000, median * 3);
  let total = 0;

  for (let i = 0; i < values.length - 1; i++) {
    const current = values[i];
    const next = values[i + 1];
    const dt = next.timestamp - current.timestamp;

    if (dt <= 0 || dt > maxGap) continue;

    const beyond = direction === "low"
      ? current.value <= threshold
      : current.value >= threshold;

    if (beyond) total += dt;
  }

  return total;
}

function exportCsv() {
  if (!currentSamples.length) return;

  const meta = races[raceSelect.value] || {};
  const header = [
    "Zeitpunkt",
    "timestamp",
    "Zylinderkopftemperatur_C",
    "Motoroeltemperatur_C",
    "Getriebeoeltemperatur_C",
    "Oeldruck_bar",
    "Drehzahl_Umin",
    "Geschwindigkeit_kmh"
  ];

  const rows = currentSamples.map(s => [
    formatDateTime(s.timestamp, true),
    s.timestamp,
    csvNumber(s.cylinder_temp),
    csvNumber(s.oil_temp),
    csvNumber(s.gear_oil_temp),
    csvNumber(s.oil_pressure),
    csvNumber(s.rpm),
    csvNumber(s.speed_kmh)
  ]);

  const csv = "\ufeff" + [header, ...rows]
    .map(row => row.map(csvEscape).join(";"))
    .join("\r\n");

  const blob = new Blob([csv], { type: "text/csv;charset=utf-8" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = `${safeFilename(meta.name || raceSelect.value || "MF35X_Rennen")}_Auswertung.csv`;
  document.body.appendChild(a);
  a.click();
  a.remove();
  URL.revokeObjectURL(url);
}

function csvNumber(value) {
  return value == null ? "" : String(value).replace(".", ",");
}

function csvEscape(value) {
  const text = String(value ?? "");
  return /[;"\r\n]/.test(text) ? `"${text.replaceAll('"', '""')}"` : text;
}

function safeFilename(text) {
  return String(text).replace(/[<>:"/\\|?*\x00-\x1F]/g, "_").trim() || "MF35X_Rennen";
}

function numberOrNull(value) {
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}

function formatMetric(value, metric) {
  return Number(value).toLocaleString("de-AT", {
    minimumFractionDigits: metric.decimals,
    maximumFractionDigits: metric.decimals
  });
}

function formatDuration(ms) {
  if (!ms) return "0 min";
  const totalMinutes = Math.round(ms / 60000);
  const hours = Math.floor(totalMinutes / 60);
  const minutes = totalMinutes % 60;
  return hours ? `${hours} h ${minutes} min` : `${minutes} min`;
}

function formatDateTime(timestamp, seconds = false) {
  if (!timestamp) return "---";
  return new Date(Number(timestamp)).toLocaleString("de-AT", {
    day: "2-digit", month: "2-digit", year: "numeric",
    hour: "2-digit", minute: "2-digit",
    ...(seconds ? { second: "2-digit" } : {})
  });
}

function formatAxisTime(timestamp) {
  const d = new Date(Number(timestamp));
  const p = n => String(n).padStart(2, "0");
  return `${p(d.getDate())}.${p(d.getMonth()+1)} ${p(d.getHours())}:${p(d.getMinutes())}`;
}

function toLocalInputValue(timestamp) {
  const d = new Date(Number(timestamp));
  const p = n => String(n).padStart(2, "0");
  return `${d.getFullYear()}-${p(d.getMonth()+1)}-${p(d.getDate())}T${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`;
}

function setStatus(text, state = "") {
  const el = document.getElementById("analysisStatus");
  el.textContent = text;
  el.className = "config-status";
  if (state) el.classList.add(`config-status-${state}`);
}

function escapeHtml(text) {
  return String(text).replace(/[&<>"']/g, c => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#039;"
  }[c]));
}
