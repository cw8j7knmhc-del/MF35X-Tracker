import { getApps, getApp, initializeApp } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js";
import { getDatabase, ref, set } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-database.js";
import { firebaseConfig } from "./firebase-config.js";

const DEFAULT_OUTPUT_CONFIG = {
  speed_enable_kmh: 60,
  rpm_on: 3200,
  rpm_off: 3150
};

const app = getApps().length ? getApp() : initializeApp(firebaseConfig);
const db = getDatabase(app);

const button = document.getElementById("resetOutputConfig");

button?.addEventListener("click", async event => {
  event.preventDefault();
  event.stopImmediatePropagation();

  const speedInput = document.getElementById("setOutputSpeedEnableKmh");
  const rpmOnInput = document.getElementById("setOutputRpmOn");
  const rpmOffInput = document.getElementById("setOutputRpmOff");
  const status = document.getElementById("outputConfigStatus");

  const originalText = button.textContent;
  button.disabled = true;
  button.textContent = "Wird geladen...";

  try {
    if (speedInput) speedInput.value = DEFAULT_OUTPUT_CONFIG.speed_enable_kmh;
    if (rpmOnInput) rpmOnInput.value = DEFAULT_OUTPUT_CONFIG.rpm_on;
    if (rpmOffInput) rpmOffInput.value = DEFAULT_OUTPUT_CONFIG.rpm_off;

    await set(ref(db, "tracker/config/external_output"), DEFAULT_OUTPUT_CONFIG);

    if (status) {
      status.textContent = "Standardwerte 60 / 3200 / 3150 geladen und gespeichert.";
      status.className = "config-status config-status-success";
    }
  } catch (error) {
    if (status) {
      status.textContent = "Standardwerte konnten nicht gespeichert werden: " + error.message;
      status.className = "config-status config-status-error";
    }
  } finally {
    button.disabled = false;
    button.textContent = originalText;
  }
}, true);
