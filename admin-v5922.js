/* MF35X Admin - V5.9.22 Erweiterungen
 * Punkt 11: festes Diagnose-CSV-Schema mit immer identischen Spalten.
 * Punkt 12: WLAN/Gateway/DNS/Firebase/RUT200-LTE Diagnose anzeigen.
 *
 * Dieses Modul wird nur auf admin.html geladen. Es greift den CSV-Klick auf
 * window-capture-Ebene ab, damit auch ältere Export-Handler keine abweichende
 * Spaltenstruktur mehr erzeugen können.
 */

import { getApps, getApp } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js";
import {
  getDatabase,
  ref,
  get,
  onValue,
  query,
  orderByChild,
  startAt,
  endAt
} from "https://www.gstatic.com/firebasejs/10.12.2/firebase-database.js";

const CSV_SCHEMA_VERSION = "MF35X_RACE_CSV_V2";
let db = null;
let csvBusy = false;

function database() {
  if (db) return db;
  if (!getApps().length) return null;
  db = getDatabase(getApp());
  return db;
}

function csvNumber(value) {
  if (value === undefined || value === null || value === "") return "";
  const n = Number(value);
  return Number.isFinite(n) ? String(n).replace(".", ",") : "";
}

function csvBool(value) {
  return value === true ? "JA" : value === false ? "NEIN" : "";
}

function csvText(value) {
  return value === undefined || value === null ? "" : String(value);
}

function csvEscape(value) {
  const text = String(value ?? "");
  return /[;"\r\n]/.test(text) ? `"${text.replaceAll('"', '""')}"` : text;
}

function safeFilename(value) {
  return String(value || "MF35X_Rennen")
    .replace(/[<>:"/\\|?*\x00-\x1F]/g, "_")
    .trim() || "MF35X_Rennen";
}

function formatDateTime(timestamp) {
  const n = Number(timestamp);
  if (!Number.isFinite(n) || n <= 0) return "";
  return new Date(n).toLocaleString("de-AT", {
    day: "2-digit",
    month: "2-digit",
    year: "numeric",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit"
  });
}

const FIXED_HEADER = [
  "CSV_Schema",
  "Zeitpunkt",
  "timestamp",

  "GPS_Valid",
  "Latitude",
  "Longitude",
  "Geschwindigkeit_kmh",
  "GPS_HDOP",
  "GPS_Satelliten",

  "Drehzahl_Anzeige_Umin",
  "Zylinderkopftemperatur_C",
  "Motoroeltemperatur_C",
  "Getriebeoeltemperatur_C",
  "Oeldruck_final_bar",
  "Batterie_V",

  "WLAN_RSSI_dBm",
  "Netz_WLAN_verbunden",
  "Netz_WLAN_RSSI_dBm",
  "Netz_Gateway_erreichbar",
  "Netz_Gateway_Latenz_ms",
  "Netz_DNS_ok",
  "Netz_DNS_Latenz_ms",
  "Netz_Firebase_ok",
  "Netz_Firebase_HTTP",
  "Netz_Firebase_Latenz_ms",
  "Netz_Fehler_in_Folge",
  "Netz_WLAN_Reconnects",
  "Netz_Internet_Fehler",
  "Netz_Probe_Alter_ms",

  "LTE_API_konfiguriert",
  "LTE_API_ok",
  "LTE_RSSI_dBm",
  "LTE_RSRP_dBm",
  "LTE_RSRQ_dB",
  "LTE_SINR_dB",
  "LTE_Operator",
  "LTE_Netztyp",

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

function fixedRow(sample) {
  const gpioDiag = typeof sample.gpio11 === "boolean"
    ? sample.gpio11
    : typeof sample.switch_output === "boolean"
      ? sample.switch_output
      : null;

  return [
    CSV_SCHEMA_VERSION,
    formatDateTime(sample.timestamp),
    csvNumber(sample.timestamp),

    csvBool(sample.gps_valid),
    csvNumber(sample.lat),
    csvNumber(sample.lng),
    csvNumber(sample.speed_kmh),
    csvNumber(sample.hdop),
    csvNumber(sample.satellites),

    csvNumber(sample.rpm),
    csvNumber(sample.cylinder_temp),
    csvNumber(sample.oil_temp),
    csvNumber(sample.gear_oil_temp),
    csvNumber(sample.oil_pressure),
    csvNumber(sample.battery_v),

    csvNumber(sample.wifi_rssi),
    csvBool(sample.net_wifi_connected),
    csvNumber(sample.net_wifi_rssi_dbm),
    csvBool(sample.net_gateway_reachable),
    csvNumber(sample.net_gateway_latency_ms),
    csvBool(sample.net_dns_ok),
    csvNumber(sample.net_dns_latency_ms),
    csvBool(sample.net_firebase_ok),
    csvNumber(sample.net_firebase_http_code),
    csvNumber(sample.net_firebase_latency_ms),
    csvNumber(sample.net_failures_consecutive),
    csvNumber(sample.net_wifi_reconnects),
    csvNumber(sample.net_internet_failures),
    csvNumber(sample.net_probe_age_ms),

    csvBool(sample.lte_api_configured),
    csvBool(sample.lte_api_ok),
    csvNumber(sample.lte_rssi_dbm),
    csvNumber(sample.lte_rsrp_dbm),
    csvNumber(sample.lte_rsrq_db),
    csvNumber(sample.lte_sinr_db),
    csvText(sample.lte_operator),
    csvText(sample.lte_network_type),

    csvBool(sample.switch_output),
    csvBool(gpioDiag),

    csvNumber(sample.oil_pressure_raw_adc),
    csvNumber(sample.oil_pressure_mv),
    csvNumber(sample.oil_pressure_ohm),
    csvNumber(sample.oil_pressure_raw_bar),
    csvText(sample.oil_pressure_diag || sample.oil_pressure_diag_status),
    csvNumber(sample.oil_pressure_fixed_resistor_ohm),

    csvNumber(sample.rpm_raw_unfiltered),
    csvNumber(sample.rpm_filtered),
    csvNumber(sample.rpm_display),
    csvNumber(sample.rpm_raw_edges_total),
    csvNumber(sample.rpm_accepted_edges_total),
    csvNumber(sample.rpm_rejected_edges_total),
    csvNumber(sample.rpm_double_edges_total),
    csvNumber(sample.rpm_reacquire_total),
    csvNumber(sample.rpm_reference_period_us),
    csvNumber(sample.rpm_rejected_since_last_sample),
    csvNumber(sample.rpm_double_since_last_sample),
    csvNumber(sample.rpm_period_count),
    csvBool(sample.rpm_filter_locked),
    csvBool(sample.rpm_signal_ok),

    csvText(sample.sample_id),
    csvNumber(sample.sample_boot_id),
    csvNumber(sample.sample_sequence),
    csvNumber(sample.captured_uptime_ms),
    csvBool(sample.capture_time_valid),
    csvText(sample.timestamp_source),
    csvBool(sample.buffered_replay)
  ];
}

async function exportFixedCsv() {
  const databaseRef = database();
  if (!databaseRef) throw new Error("Firebase ist noch nicht bereit.");

  const raceSelect = document.getElementById("raceSelect");
  const raceId = raceSelect?.value || "";
  if (!raceId) throw new Error("Bitte zuerst eine Rennaufzeichnung auswählen.");

  const metaSnapshot = await get(ref(databaseRef, `tracker/races/${raceId}`));
  const meta = metaSnapshot.val() || {};

  const fromValue = document.getElementById("fromTime")?.value || "";
  const toValue = document.getElementById("toTime")?.value || "";
  let start = fromValue ? new Date(fromValue).getTime() : Number(meta.startedAt || 0);
  let stop = toValue ? new Date(toValue).getTime() : Number(meta.stoppedAt || Date.now());
  if (!Number.isFinite(start) || !Number.isFinite(stop) || stop <= start) {
    throw new Error("Ungültiger Zeitraum für den CSV-Export.");
  }

  const sampleQuery = query(
    ref(databaseRef, `tracker/races/${raceId}/samples`),
    orderByChild("timestamp"),
    startAt(start),
    endAt(stop)
  );
  const snapshot = await get(sampleQuery);
  const samples = Object.values(snapshot.val() || {})
    .filter(sample => sample && Number.isFinite(Number(sample.timestamp)))
    .sort((a, b) => Number(a.timestamp) - Number(b.timestamp));

  if (!samples.length) throw new Error("Im ausgewählten Zeitraum sind keine Rennmesswerte vorhanden.");

  const rows = samples.map(fixedRow);
  const csv = "\ufeff" + [FIXED_HEADER, ...rows]
    .map(row => row.map(csvEscape).join(";"))
    .join("\r\n");

  const blob = new Blob([csv], { type: "text/csv;charset=utf-8" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = `${safeFilename(meta.name || raceId)}_Auswertung_Diagnose.csv`;
  document.body.appendChild(a);
  a.click();
  a.remove();
  URL.revokeObjectURL(url);
}

// window-capture liegt vor den älteren element-capture-Handlern.
window.addEventListener("click", async event => {
  const button = event.target?.closest?.("#exportCsv");
  if (!button) return;

  event.preventDefault();
  event.stopImmediatePropagation();
  if (csvBusy) return;
  csvBusy = true;

  const original = button.innerHTML;
  button.disabled = true;
  button.textContent = "CSV wird erstellt...";
  try {
    await exportFixedCsv();
  } catch (error) {
    console.error("MF35X CSV V2:", error);
    alert("CSV-Export fehlgeschlagen: " + error.message);
  } finally {
    button.disabled = false;
    button.innerHTML = original;
    csvBusy = false;
  }
}, true);

function badge(element, text, ok, warn = false) {
  if (!element) return;
  element.textContent = text;
  element.className = "recording-badge " +
    (ok ? "recording-badge-on" : warn ? "recording-badge-wait" : "recording-badge-wait");
}

function finite(value) {
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}

function createConnectivityPanel() {
  if (document.querySelector("[data-mf35x-connectivity-panel]")) return;
  const anchor = document.getElementById("raceAnalysis");
  if (!anchor) return;

  const section = document.createElement("section");
  section.className = "settings admin-settings";
  section.dataset.mf35xConnectivityPanel = "1";
  section.innerHTML = `
    <h2>Verbindungsdiagnose</h2>
    <div class="recording-status-row">
      <span>WLAN zum RUT200:</span>
      <strong id="netWifiBadge" class="recording-badge recording-badge-wait">wird geladen</strong>
      <span>Internet / Firebase:</span>
      <strong id="netFirebaseBadge" class="recording-badge recording-badge-wait">wird geladen</strong>
      <span>RUT200 LTE:</span>
      <strong id="netLteBadge" class="recording-badge recording-badge-wait">wird geladen</strong>
    </div>
    <p id="netConnectivityDetail" class="settings-note settings-note-block">
      Warte auf ESP32-Netzwerkdiagnose.
    </p>
    <p id="netLteDetail" class="settings-note settings-note-block">
      RUT200-Mobilfunkwerte werden angezeigt, sobald lokale API-Zugangsdaten im ESP32 hinterlegt sind.
    </p>`;
  anchor.insertAdjacentElement("beforebegin", section);
}

function renderConnectivity(value) {
  createConnectivityPanel();
  const wifi = value?.wifi_connected === true;
  const gateway = value?.gateway_reachable === true;
  const dns = value?.dns_ok === true;
  const firebaseOk = value?.firebase_ok === true;
  const apiConfigured = value?.rut200_api_configured === true;
  const apiOk = value?.rut200_api_ok === true;

  badge(document.getElementById("netWifiBadge"), wifi ? "verbunden" : "getrennt", wifi);
  badge(
    document.getElementById("netFirebaseBadge"),
    firebaseOk ? "erreichbar" : wifi ? "nicht erreichbar" : "kein WLAN",
    firebaseOk,
    true
  );
  badge(
    document.getElementById("netLteBadge"),
    apiOk ? "Messwerte aktiv" : apiConfigured ? "API nicht erreichbar" : "API nicht konfiguriert",
    apiOk,
    true
  );

  const detail = document.getElementById("netConnectivityDetail");
  if (detail) {
    const rssi = finite(value?.wifi_rssi_dbm);
    const gwMs = finite(value?.gateway_latency_ms);
    const dnsMs = finite(value?.dns_latency_ms);
    const fbMs = finite(value?.firebase_latency_ms);
    const http = finite(value?.firebase_http_code);
    const failures = finite(value?.failures_consecutive) ?? 0;
    const reconnects = finite(value?.wifi_reconnects) ?? 0;

    detail.textContent =
      `WLAN ${wifi ? "OK" : "FEHLER"}${rssi != null ? ` · ${rssi} dBm` : ""}` +
      ` · Gateway ${gateway ? "OK" : "FEHLER"}${gwMs != null ? ` ${gwMs} ms` : ""}` +
      ` · DNS ${dns ? "OK" : "FEHLER"}${dnsMs != null ? ` ${dnsMs} ms` : ""}` +
      ` · Firebase ${firebaseOk ? "OK" : "FEHLER"}${http != null ? ` HTTP ${http}` : ""}${fbMs != null ? ` · ${fbMs} ms` : ""}` +
      ` · Fehler in Folge ${failures} · WLAN-Reconnects ${reconnects}`;
  }

  const lte = document.getElementById("netLteDetail");
  if (lte) {
    if (!apiConfigured) {
      lte.textContent =
        "Direkte LTE-Werte optional: MF35X_RUT200_PASSWORD nur lokal in secrets.h hinterlegen. Passwort und API-Token werden nie zu Firebase übertragen.";
    } else if (!apiOk) {
      lte.textContent = "RUT200-API ist konfiguriert, liefert derzeit aber keine Modemwerte.";
    } else {
      const parts = [];
      if (value.lte_operator) parts.push(`Operator ${value.lte_operator}`);
      if (value.lte_network_type) parts.push(`Netz ${value.lte_network_type}`);
      if (finite(value.lte_rssi_dbm) != null) parts.push(`RSSI ${value.lte_rssi_dbm} dBm`);
      if (finite(value.lte_rsrp_dbm) != null) parts.push(`RSRP ${value.lte_rsrp_dbm} dBm`);
      if (finite(value.lte_rsrq_db) != null) parts.push(`RSRQ ${value.lte_rsrq_db} dB`);
      if (finite(value.lte_sinr_db) != null) parts.push(`SINR ${value.lte_sinr_db} dB`);
      lte.textContent = parts.length ? parts.join(" · ") : "RUT200-API erreichbar; noch keine verwertbaren Modemwerte.";
    }
  }
}

async function setupConnectivityListener() {
  for (let i = 0; i < 80 && !database(); i++) {
    await new Promise(resolve => setTimeout(resolve, 50));
  }
  const databaseRef = database();
  if (!databaseRef) return;
  createConnectivityPanel();
  onValue(ref(databaseRef, "tracker/device/connectivity"), snapshot => {
    renderConnectivity(snapshot.val() || {});
  });
}

setupConnectivityListener().catch(error => {
  console.warn("Verbindungsdiagnose konnte nicht initialisiert werden:", error);
});
