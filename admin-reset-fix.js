import { getApps, getApp, initializeApp } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js";
import {
  getDatabase,
  ref,
  set,
  get,
  query,
  orderByChild,
  startAt,
  endAt
} from "https://www.gstatic.com/firebasejs/10.12.2/firebase-database.js";
import { firebaseConfig } from "./firebase-config.js";

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
  rpm_on: 2500,
  rpm_off: 2450
};

// Muss mit dem aktuellen ESP32-Referenzstand übereinstimmen.
// Die interne GPS-Verarbeitung bleibt 10 Hz; 1000 ms betrifft nur den
// Firebase-/Website-Upload der GPS-Daten.
const DEFAULT_INTERVALS = {
  rpm_firebase_update_ms: 250,
  oil_pressure_update_ms: 100,
  temperature_update_ms: 1000,
  gps_update_ms: 1000,
  history_update_ms: 5000
};

const app = getApps().length ? getApp() : initializeApp(firebaseConfig);
const db = getDatabase(app);

function setField(id, value) {
  const element = document.getElementById(id);
  if (element) element.value = value;
}

function setStatus(id, text, state = "") {
  const element = document.getElementById(id);
  if (!element) return;
  element.textContent = text;
  element.className = "config-status";
  if (state) element.classList.add(`config-status-${state}`);
}

function withResetButton(button, workingText, action) {
  button?.addEventListener("click", async event => {
    event.preventDefault();
    event.stopImmediatePropagation();

    const originalText = button.textContent;
    button.disabled = true;
    button.textContent = workingText;

    try {
      await action();
    } finally {
      button.disabled = false;
      button.textContent = originalText;
    }
  }, true);
}

function raceCsvNumber(value) {
  if (value === undefined || value === null || value === "") return "";
  const number = Number(value);
  return Number.isFinite(number) ? String(number).replace(".", ",") : "";
}

function raceCsvBoolean(value) {
  return value === true ? "JA" : value === false ? "NEIN" : "";
}

function raceCsvEscape(value) {
  const text = String(value ?? "");
  return /[;"\r\n]/.test(text) ? `"${text.replaceAll('"', '""')}"` : text;
}

function raceSafeFilename(text) {
  return String(text).replace(/[<>:"/\\|?*\x00-\x1F]/g, "_").trim() || "MF35X_Rennen";
}

function raceFormatDateTime(timestamp) {
  const value = Number(timestamp);
  if (!Number.isFinite(value) || value <= 0) return "";
  return new Date(value).toLocaleString("de-AT", {
    day: "2-digit",
    month: "2-digit",
    year: "numeric",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit"
  });
}

async function exportDetailedRaceCsv() {
  const raceSelect = document.getElementById("raceSelect");
  const fromInput = document.getElementById("fromTime");
  const toInput = document.getElementById("toTime");
  const raceId = raceSelect?.value || "";

  if (!raceId) {
    alert("Bitte zuerst eine Rennaufzeichnung auswählen.");
    return;
  }

  let start = fromInput?.value ? new Date(fromInput.value).getTime() : NaN;
  let stop = toInput?.value ? new Date(toInput.value).getTime() : NaN;

  const metaSnapshot = await get(ref(db, `tracker/races/${raceId}`));
  const meta = metaSnapshot.val() || {};

  if (!Number.isFinite(start)) start = Number(meta.startedAt || 0);
  if (!Number.isFinite(stop)) stop = Number(meta.stoppedAt || Date.now());

  if (!Number.isFinite(start) || !Number.isFinite(stop) || stop <= start) {
    throw new Error("Ungültiger Zeitraum für den CSV-Export");
  }

  const samplesQuery = query(
    ref(db, `tracker/races/${raceId}/samples`),
    orderByChild("timestamp"),
    startAt(start),
    endAt(stop)
  );
  const samplesSnapshot = await get(samplesQuery);
  const raw = samplesSnapshot.val() || {};
  const samples = Object.values(raw)
    .filter(sample => sample && Number.isFinite(Number(sample.timestamp)))
    .sort((a, b) => Number(a.timestamp) - Number(b.timestamp));

  if (!samples.length) {
    alert("Für den ausgewählten Zeitraum sind keine Rennmesswerte vorhanden.");
    return;
  }

  const header = [
    "Zeitpunkt",
    "timestamp",
    "Zylinderkopftemperatur_C",
    "Motoroeltemperatur_C",
    "Getriebeoeltemperatur_C",
    "Oeldruck_final_bar",
    "Drehzahl_Anzeige_Umin",
    "Geschwindigkeit_kmh",
    "GPS_Valid",
    "GPS_Latitude",
    "GPS_Longitude",
    "Batterie_V",
    "GPS_HDOP",
    "GPS_Satelliten",
    "WLAN_RSSI_dBm",
    "GPIO11_switch_output",
    "GPIO11_Diagnose",

    "Oeldruck_AIN1_ADC",
    "Oeldruck_AIN1_mV",
    "Oeldruck_Geber_Ohm",
    "Oeldruck_vor_Begrenzung_bar",
    "Oeldruck_Diagnose",
    "Oeldruck_Festwiderstand_Ohm",

    "RPM_raw_ungefiltert_Umin",
    "RPM_gefiltert_schnell_Umin",
    "RPM_display_Umin",
    "RPM_raw_edges_total",
    "RPM_accepted_edges_total",
    "RPM_rejected_edges_total",
    "RPM_double_edges_total",
    "RPM_reacquire_total",
    "RPM_reference_period_us",
    "RPM_rejected_seit_letztem_Sample",
    "RPM_double_seit_letztem_Sample",
    "RPM_period_count",
    "RPM_filter_locked",
    "RPM_signal_ok",

    "Sample_ID",
    "Sample_Boot_ID",
    "Sample_Sequence",
    "Capture_Uptime_ms",
    "Capture_Time_Valid",
    "Timestamp_Source",
    "Offline_nachgesendet"
  ];

  const rows = samples.map(sample => {
    const gpio11 = typeof sample.gpio11 === "boolean"
      ? sample.gpio11
      : typeof sample.switch_output === "boolean"
        ? sample.switch_output
        : null;

    return [
      raceFormatDateTime(sample.timestamp),
      sample.timestamp,
      raceCsvNumber(sample.cylinder_temp),
      raceCsvNumber(sample.oil_temp),
      raceCsvNumber(sample.gear_oil_temp),
      raceCsvNumber(sample.oil_pressure),
      raceCsvNumber(sample.rpm),
      raceCsvNumber(sample.speed_kmh),
      raceCsvBoolean(sample.gps_valid),
      raceCsvNumber(sample.lat),
      raceCsvNumber(sample.lng),
      raceCsvNumber(sample.battery_v),
      raceCsvNumber(sample.hdop),
      raceCsvNumber(sample.satellites),
      raceCsvNumber(sample.wifi_rssi),
      raceCsvBoolean(sample.switch_output),
      raceCsvBoolean(gpio11),

      raceCsvNumber(sample.oil_pressure_raw_adc),
      raceCsvNumber(sample.oil_pressure_mv),
      raceCsvNumber(sample.oil_pressure_ohm),
      raceCsvNumber(sample.oil_pressure_raw_bar),
      sample.oil_pressure_diag || sample.oil_pressure_diag_status || "",
      raceCsvNumber(sample.oil_pressure_fixed_resistor_ohm),

      raceCsvNumber(sample.rpm_raw_unfiltered),
      raceCsvNumber(sample.rpm_filtered),
      raceCsvNumber(sample.rpm_display),
      raceCsvNumber(sample.rpm_raw_edges_total),
      raceCsvNumber(sample.rpm_accepted_edges_total),
      raceCsvNumber(sample.rpm_rejected_edges_total),
      raceCsvNumber(sample.rpm_double_edges_total),
      raceCsvNumber(sample.rpm_reacquire_total),
      raceCsvNumber(sample.rpm_reference_period_us),
      raceCsvNumber(sample.rpm_rejected_since_last_sample),
      raceCsvNumber(sample.rpm_double_since_last_sample),
      raceCsvNumber(sample.rpm_period_count),
      raceCsvBoolean(sample.rpm_filter_locked),
      raceCsvBoolean(sample.rpm_signal_ok),

      sample.sample_id || "",
      raceCsvNumber(sample.sample_boot_id),
      raceCsvNumber(sample.sample_sequence),
      raceCsvNumber(sample.captured_uptime_ms),
      raceCsvBoolean(sample.capture_time_valid),
      sample.timestamp_source || "",
      raceCsvBoolean(sample.buffered_replay)
    ];
  });

  const csv = "\ufeff" + [header, ...rows]
    .map(row => row.map(raceCsvEscape).join(";"))
    .join("\r\n");

  const blob = new Blob([csv], { type: "text/csv;charset=utf-8" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = `${raceSafeFilename(meta.name || raceId)}_Auswertung_Diagnose.csv`;
  document.body.appendChild(a);
  a.click();
  a.remove();
  URL.revokeObjectURL(url);
}

// Den Hinweis in der Adminoberfläche an den tatsächlichen Firmware-Standard angleichen.
const gpsInput = document.getElementById("setGpsUpdateMs");
const gpsHint = gpsInput?.closest("label")?.querySelector(".field-hint");
if (gpsHint) {
  gpsHint.textContent =
    "Position und Geschwindigkeit gemeinsam · einstellbar 100–3000 ms · Standard 1000 ms";
}

// Falls noch überhaupt keine Intervall-Konfiguration existiert, vor dem Start der
// Admin-Listener den korrekten Firmware-Standard anlegen. Bestehende Benutzerwerte
// werden ausdrücklich nicht verändert.
try {
  const intervalSnapshot = await get(ref(db, "tracker/config/intervals"));
  if (!intervalSnapshot.exists()) {
    await set(ref(db, "tracker/config/intervals"), DEFAULT_INTERVALS);
  }
} catch (error) {
  console.warn("Intervall-Standard konnte nicht vorab geprüft werden:", error);
}

// --------------------------------------------------
// Maximalwerte: ausschließlich den ESP32 zurücksetzen
// --------------------------------------------------
// Der ESP32 verwaltet seine Maximalwerte zentral in NVS und Firebase.
// Dieser Capture-Handler stellt sicher, dass der Reset ausschließlich über
// den V5.9.13+-Systembefehl an den ESP32 läuft.
withResetButton(
  document.getElementById("resetMaxValues"),
  "Wird zurückgesetzt...",
  async () => {
    if (!confirm("Maximalwerte wirklich zurücksetzen? Der ESP32 beginnt danach sofort mit einer neuen Erfassung.")) {
      return;
    }

    const requestedAt = Date.now();
    const requestId =
      `maxreset_${requestedAt}_${Math.random().toString(36).slice(2, 10)}`;

    try {
      setStatus(
        "systemCommandStatus",
        "Maximalwerte zurücksetzen: Befehl wird an den ESP32 gesendet …",
        "pending"
      );

      await set(ref(db, "tracker/config/system_commands/max_values_reset"), {
        requestId,
        requestedAt,
        status: "requested"
      });

      setStatus(
        "systemCommandStatus",
        "Maximalwerte zurücksetzen: Befehl gesendet – wartet auf ESP32.",
        "pending"
      );
    } catch (error) {
      setStatus(
        "systemCommandStatus",
        "Maximalwerte zurücksetzen: Senden fehlgeschlagen – " + error.message,
        "error"
      );
      alert("Fehler beim Senden des Reset-Befehls: " + error.message);
    }
  }
);

// --------------------------------------------------
// Alarmgrenzen: Standardwerte sichtbar + Firebase
// --------------------------------------------------
withResetButton(
  document.getElementById("resetSettings"),
  "Wird geladen...",
  async () => {
    try {
      await set(ref(db, "tracker/settings"), DEFAULT_LIMITS);

      setField("setBatteryWarn", DEFAULT_LIMITS.batteryWarn);
      setField("setBatteryAlarm", DEFAULT_LIMITS.batteryAlarm);
      setField("setOilPressureWarn", DEFAULT_LIMITS.oilPressureWarn);
      setField("setOilPressureAlarm", DEFAULT_LIMITS.oilPressureAlarm);
      setField("setOilTempWarn", DEFAULT_LIMITS.oilTempWarn);
      setField("setOilTempAlarm", DEFAULT_LIMITS.oilTempAlarm);
      setField("setCylTempWarn", DEFAULT_LIMITS.cylTempWarn);
      setField("setCylTempAlarm", DEFAULT_LIMITS.cylTempAlarm);

      alert("Standardwerte der Alarmgrenzen geladen und gespeichert.");
    } catch (error) {
      alert("Standardwerte der Alarmgrenzen konnten nicht gespeichert werden: " + error.message);
    }
  }
);

// --------------------------------------------------
// Externer Ausgang: Standardwerte sichtbar + Firebase
// --------------------------------------------------
withResetButton(
  document.getElementById("resetOutputConfig"),
  "Wird geladen...",
  async () => {
    try {
      await set(ref(db, "tracker/config/external_output"), DEFAULT_OUTPUT_CONFIG);

      setField("setOutputSpeedEnableKmh", DEFAULT_OUTPUT_CONFIG.speed_enable_kmh);
      setField("setOutputRpmOn", DEFAULT_OUTPUT_CONFIG.rpm_on);
      setField("setOutputRpmOff", DEFAULT_OUTPUT_CONFIG.rpm_off);

      setStatus(
        "outputConfigStatus",
        "Standardwerte 60 / 2500 / 2450 geladen und gespeichert.",
        "success"
      );
    } catch (error) {
      setStatus(
        "outputConfigStatus",
        "Standardwerte konnten nicht gespeichert werden: " + error.message,
        "error"
      );
    }
  }
);

// --------------------------------------------------
// Aktualisierungsintervalle: Standardwerte sichtbar + Firebase
// --------------------------------------------------
withResetButton(
  document.getElementById("resetIntervals"),
  "Wird geladen...",
  async () => {
    try {
      await set(ref(db, "tracker/config/intervals"), DEFAULT_INTERVALS);

      setField("setRpmFirebaseUpdateMs", DEFAULT_INTERVALS.rpm_firebase_update_ms);
      setField("setOilPressureUpdateMs", DEFAULT_INTERVALS.oil_pressure_update_ms);
      setField("setTemperatureUpdateMs", DEFAULT_INTERVALS.temperature_update_ms);
      setField("setGpsUpdateMs", DEFAULT_INTERVALS.gps_update_ms);
      setField("setHistoryUpdateMs", DEFAULT_INTERVALS.history_update_ms);

      setStatus(
        "intervalStatus",
        "Standardintervalle 250 / 100 / 1000 / 1000 / 5000 ms geladen und gespeichert.",
        "success"
      );
    } catch (error) {
      setStatus(
        "intervalStatus",
        "Standardintervalle konnten nicht gespeichert werden: " + error.message,
        "error"
      );
    }
  }
);

// --------------------------------------------------
// Renn-CSV: vollständiger Diagnoseexport
// --------------------------------------------------
// admin.js normalisiert für Diagramme bewusst nur wenige Standardfelder.
// Der Capture-Handler liest deshalb die Original-Samples direkt aus Firebase,
// damit RPM-Rohdaten, Filterzähler, GPIO11 und Öldruckdiagnose nicht verloren
// gehen. Die Besucheransicht bleibt davon vollständig unberührt.
const detailedExportButton = document.getElementById("exportCsv");
detailedExportButton?.addEventListener("click", async event => {
  event.preventDefault();
  event.stopImmediatePropagation();

  const originalText = detailedExportButton.textContent;
  detailedExportButton.disabled = true;
  detailedExportButton.textContent = "CSV wird erstellt...";

  try {
    await exportDetailedRaceCsv();
  } catch (error) {
    console.error("Diagnose-CSV fehlgeschlagen:", error);
    alert("CSV-Export fehlgeschlagen: " + error.message);
  } finally {
    detailedExportButton.disabled = false;
    detailedExportButton.textContent = originalText;
  }
}, true);
