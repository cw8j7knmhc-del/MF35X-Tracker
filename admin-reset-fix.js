import { getApps, getApp, initializeApp } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js";
import { getDatabase, ref, set, get } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-database.js";
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
  rpm_on: 3200,
  rpm_off: 3150
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
        "Standardwerte 60 / 3200 / 3150 geladen und gespeichert.",
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
