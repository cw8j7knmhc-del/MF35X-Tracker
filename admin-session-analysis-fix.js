/* MF35X Admin-Komfort + Rennauswertung Öldruck
 * - optionaler 30-Tage-Loginmerker, Passwort wird NICHT in Web Storage gespeichert
 * - korrigiert nur die historischen Öldruck-Grenzzeiten
 * - Motorlauf: RPM >= 400 und 5 s Startverzögerung
 * - keine ESP32-/Firebase-Schreibzugriffe
 */

import { getApps, getApp } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js";
import { getDatabase, ref, get, query, orderByChild, startAt, endAt } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-database.js";

const ADMIN_REMEMBER_KEY = "mf35x_admin_remember_v1";
const ADMIN_REMEMBER_MS = 30 * 24 * 60 * 60 * 1000;
const OIL_PRESSURE_RPM_MIN = 400;
const OIL_PRESSURE_START_DELAY_MS = 5000;
const ANALYSIS_LOOKBACK_MS = 15000;

let adminExpiryTimer = null;
let autoLoginInProgress = false;
let oilFixTimer = null;
let oilFixRunning = false;

setupAdminRememberLogin();
setupOilPressureAnalysisFix();

function setupAdminRememberLogin() {
  const loginBox = document.getElementById("loginBox");
  const adminContent = document.getElementById("adminContent");
  const passwordInput = document.getElementById("adminPassword");
  const loginButton = document.getElementById("loginButton");
  if (!loginBox || !adminContent || !passwordInput || !loginButton) return;

  const rememberLabel = document.createElement("label");
  rememberLabel.className = "switch-row";
  rememberLabel.style.margin = "12px 0";

  const rememberText = document.createElement("span");
  rememberText.textContent = "Auf diesem Gerät angemeldet bleiben (30 Tage)";

  const rememberInput = document.createElement("input");
  rememberInput.type = "checkbox";
  rememberInput.id = "adminRememberLogin";

  const slider = document.createElement("span");
  slider.className = "slider";
  rememberLabel.append(rememberText, rememberInput, slider);

  loginButton.insertAdjacentElement("beforebegin", rememberLabel);

  const logoutButton = document.createElement("button");
  logoutButton.type = "button";
  logoutButton.className = "reset-button";
  logoutButton.textContent = "Admin abmelden";
  logoutButton.style.marginLeft = "8px";

  const visitorLink = adminContent.querySelector('a[href="index.html"]');
  if (visitorLink) visitorLink.insertAdjacentElement("afterend", logoutButton);
  else adminContent.prepend(logoutButton);

  logoutButton.addEventListener("click", () => {
    clearAdminRemember();
    location.reload();
  });

  const observer = new MutationObserver(() => {
    const loggedIn = loginBox.classList.contains("hidden") && !adminContent.classList.contains("hidden");
    if (!loggedIn) return;

    if (!autoLoginInProgress) {
      if (rememberInput.checked) {
        saveAdminRemember(Date.now() + ADMIN_REMEMBER_MS);
      } else {
        clearAdminRemember();
      }
    }

    passwordInput.value = "";
  });
  observer.observe(loginBox, { attributes: true, attributeFilter: ["class"] });
  observer.observe(adminContent, { attributes: true, attributeFilter: ["class"] });

  const expiresAt = readAdminRememberExpiry();
  if (expiresAt > Date.now()) {
    rememberInput.checked = true;
    scheduleAdminExpiry(expiresAt);
    autoLoginInProgress = true;
    autoLoginFromExistingAdminSource(passwordInput, loginButton)
      .catch(error => {
        console.warn("Automatischer Admin-Login nicht möglich:", error);
        clearAdminRemember();
        rememberInput.checked = false;
      })
      .finally(() => {
        setTimeout(() => { autoLoginInProgress = false; }, 0);
      });
  } else if (expiresAt) {
    clearAdminRemember();
  }
}

async function autoLoginFromExistingAdminSource(passwordInput, loginButton) {
  const response = await fetch(`./admin.js?remember=${Date.now()}`, { cache: "no-store" });
  if (!response.ok) throw new Error(`admin.js HTTP ${response.status}`);

  const source = await response.text();
  const match = source.match(/const\s+ADMIN_PASSWORD\s*=\s*("(?:\\.|[^"\\])*")\s*;/);
  if (!match) throw new Error("Admin-Passwortkonstante nicht gefunden.");

  const password = JSON.parse(match[1]);
  passwordInput.value = password;
  loginButton.click();
  passwordInput.value = "";
}

function saveAdminRemember(expiresAt) {
  try {
    localStorage.setItem(ADMIN_REMEMBER_KEY, JSON.stringify({ expiresAt }));
    scheduleAdminExpiry(expiresAt);
  } catch (error) {
    console.warn("Admin-Anmeldung konnte nicht lokal gemerkt werden:", error);
  }
}

function readAdminRememberExpiry() {
  try {
    const raw = localStorage.getItem(ADMIN_REMEMBER_KEY);
    if (!raw) return 0;
    const value = JSON.parse(raw);
    const expiresAt = Number(value?.expiresAt || 0);
    return Number.isFinite(expiresAt) ? expiresAt : 0;
  } catch {
    return 0;
  }
}

function clearAdminRemember() {
  if (adminExpiryTimer) clearTimeout(adminExpiryTimer);
  adminExpiryTimer = null;
  try { localStorage.removeItem(ADMIN_REMEMBER_KEY); } catch {}
}

function scheduleAdminExpiry(expiresAt) {
  if (adminExpiryTimer) clearTimeout(adminExpiryTimer);
  const remaining = expiresAt - Date.now();

  if (remaining <= 0) {
    clearAdminRemember();
    location.reload();
    return;
  }

  // Browser-Timer sind auf rund 24,8 Tage begrenzt. Bei 30 Tagen daher
  // zwischendurch nur neu planen, nicht vorzeitig abmelden.
  const delay = Math.min(remaining, 24 * 60 * 60 * 1000);
  adminExpiryTimer = setTimeout(() => scheduleAdminExpiry(expiresAt), delay);
}

function setupOilPressureAnalysisFix() {
  const statsGrid = document.getElementById("statsGrid");
  if (!statsGrid) return;

  const observer = new MutationObserver(() => scheduleOilFix());
  observer.observe(statsGrid, { childList: true, subtree: true, characterData: true });

  document.getElementById("loadRange")?.addEventListener("click", () => scheduleOilFix(350));
  document.getElementById("loadFullRace")?.addEventListener("click", () => scheduleOilFix(350));
  document.getElementById("raceSelect")?.addEventListener("change", () => scheduleOilFix(350));
}

function scheduleOilFix(delay = 120) {
  if (oilFixRunning) return;
  if (oilFixTimer) clearTimeout(oilFixTimer);
  oilFixTimer = setTimeout(() => {
    oilFixTimer = null;
    correctOilPressureDurations().catch(error => {
      console.warn("Öldruck-Grenzzeit konnte nicht korrigiert werden:", error);
    });
  }, delay);
}

async function correctOilPressureDurations() {
  if (oilFixRunning) return;

  const statsGrid = document.getElementById("statsGrid");
  const raceId = document.getElementById("raceSelect")?.value || "";
  const fromValue = document.getElementById("fromTime")?.value || "";
  const toValue = document.getElementById("toTime")?.value || "";
  if (!statsGrid || !raceId || !fromValue || !toValue) return;

  const start = new Date(fromValue).getTime();
  const stop = new Date(toValue).getTime();
  if (!Number.isFinite(start) || !Number.isFinite(stop) || stop <= start) return;

  const oilCard = [...statsGrid.querySelectorAll(".stat-card")].find(card =>
    card.querySelector(".stat-title")?.textContent?.trim() === "Öldruck"
  );
  if (!oilCard) return;

  oilFixRunning = true;
  try {
    const db = await waitForDatabase();
    const historyQuery = query(
      ref(db, `tracker/races/${raceId}/samples`),
      orderByChild("timestamp"),
      startAt(Math.max(0, start - ANALYSIS_LOOKBACK_MS)),
      endAt(stop)
    );

    const [historySnapshot, settingsSnapshot] = await Promise.all([
      get(historyQuery),
      get(ref(db, "tracker/settings"))
    ]);

    const samples = Object.values(historySnapshot.val() || {})
      .map(sample => ({
        timestamp: finiteNumber(sample?.timestamp),
        rpm: finiteNumber(sample?.rpm),
        oilPressure: finiteNumber(sample?.oil_pressure)
      }))
      .filter(sample => sample.timestamp != null)
      .sort((a, b) => a.timestamp - b.timestamp);

    const settings = settingsSnapshot.val() || {};
    const warn = finiteNumber(settings.oilPressureWarn) ?? 2.0;
    const alarm = finiteNumber(settings.oilPressureAlarm) ?? 1.2;

    const warnMs = oilPressureDurationBelow(samples, warn, start, stop);
    const alarmMs = oilPressureDurationBelow(samples, alarm, start, stop);

    replaceStatRow(oilCard, "Warnbereich", formatDuration(warnMs));
    replaceStatRow(oilCard, "Alarmbereich", formatDuration(alarmMs));

    let hint = oilCard.querySelector("[data-mf35x-oil-analysis-hint]");
    if (!hint) {
      hint = document.createElement("div");
      hint.dataset.mf35xOilAnalysisHint = "1";
      hint.className = "field-hint";
      hint.style.marginTop = "8px";
      oilCard.appendChild(hint);
    }
    const hintText = "Öldruck-Grenzzeiten: nur bei ≥400 U/min und nach 5 s Motorlauf.";
    if (hint.textContent !== hintText) hint.textContent = hintText;
  } finally {
    oilFixRunning = false;
  }
}

async function waitForDatabase() {
  for (let i = 0; i < 40; i++) {
    if (getApps().length) return getDatabase(getApp());
    await new Promise(resolve => setTimeout(resolve, 50));
  }
  throw new Error("Firebase-App nicht initialisiert.");
}

function oilPressureDurationBelow(samples, threshold, rangeStart, rangeStop) {
  if (samples.length < 2) return 0;

  const intervals = [];
  for (let i = 1; i < samples.length; i++) {
    const dt = samples[i].timestamp - samples[i - 1].timestamp;
    if (dt > 0 && dt < 60000) intervals.push(dt);
  }
  const sorted = [...intervals].sort((a, b) => a - b);
  const median = sorted.length ? sorted[Math.floor(sorted.length / 2)] : 5000;
  const maxGap = Math.max(15000, median * 3);

  let total = 0;
  let engineRunSince = null;

  for (let i = 0; i < samples.length - 1; i++) {
    const current = samples[i];
    const next = samples[i + 1];
    const dt = next.timestamp - current.timestamp;

    if (dt <= 0 || dt > maxGap) {
      engineRunSince = null;
      continue;
    }

    const engineRunning = current.rpm != null && current.rpm >= OIL_PRESSURE_RPM_MIN;
    if (!engineRunning) {
      engineRunSince = null;
      continue;
    }

    if (engineRunSince == null) engineRunSince = current.timestamp;

    const eligibleFrom = engineRunSince + OIL_PRESSURE_START_DELAY_MS;
    const intervalStart = Math.max(current.timestamp, rangeStart, eligibleFrom);
    const intervalEnd = Math.min(next.timestamp, rangeStop);

    if (
      intervalEnd > intervalStart &&
      current.oilPressure != null &&
      current.oilPressure <= threshold
    ) {
      total += intervalEnd - intervalStart;
    }
  }

  return total;
}

function replaceStatRow(card, label, value) {
  const row = [...card.querySelectorAll(".stat-row")].find(item =>
    item.querySelector("span")?.textContent?.trim() === label
  );
  if (!row) return;
  const strong = row.querySelector("strong");
  if (strong && strong.textContent !== value) strong.textContent = value;
}

function formatDuration(ms) {
  if (!ms) return "0 min";
  const totalMinutes = Math.round(ms / 60000);
  const hours = Math.floor(totalMinutes / 60);
  const minutes = totalMinutes % 60;
  return hours ? `${hours} h ${minutes} min` : `${minutes} min`;
}

function finiteNumber(value) {
  if (value === undefined || value === null || value === "") return null;
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}
