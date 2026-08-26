from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "esp32/MF35X_Livetracker/MF35X_Livetracker_core.hpp"
HDR = ROOT / "esp32/MF35X_Livetracker/device_metrics_alarm.hpp"
VER = ROOT / "esp32/MF35X_Livetracker/firmware_version.h"
INO = ROOT / "esp32/MF35X_Livetracker/MF35X_Livetracker.ino"
SCRIPT = ROOT / "script.js"
ADMIN = ROOT / "admin.js"


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: erwartet 1 Treffer, gefunden {count}")
    return text.replace(old, new, 1)


def find_function_end(text, start):
    brace = text.find("{", start)
    if brace < 0:
        raise SystemExit("Funktionsklammer nicht gefunden")
    depth = 0
    i = brace
    quote = None
    escape = False
    line_comment = False
    block_comment = False
    while i < len(text):
        c = text[i]
        n = text[i + 1] if i + 1 < len(text) else ""
        if line_comment:
            if c == "\n":
                line_comment = False
            i += 1
            continue
        if block_comment:
            if c == "*" and n == "/":
                block_comment = False
                i += 2
                continue
            i += 1
            continue
        if quote:
            if escape:
                escape = False
            elif c == "\\":
                escape = True
            elif c == quote:
                quote = None
            i += 1
            continue
        if c == "/" and n == "/":
            line_comment = True
            i += 2
            continue
        if c == "/" and n == "*":
            block_comment = True
            i += 2
            continue
        if c in ("'", '"', "`"):
            quote = c
            i += 1
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                end = i + 1
                while end < len(text) and text[end] in " \t":
                    end += 1
                if end < len(text) and text[end] == "\r":
                    end += 1
                if end < len(text) and text[end] == "\n":
                    end += 1
                return end
        i += 1
    raise SystemExit("Funktionsende nicht gefunden")


def replace_function(text, name, new_text):
    m = re.search(rf"(?:async\s+)?function\s+{re.escape(name)}\s*\(", text)
    if not m:
        raise SystemExit(f"Funktion {name} nicht gefunden")
    end = find_function_end(text, m.start())
    return text[:m.start()] + new_text.rstrip() + "\n\n" + text[end:]


# ---------- device_metrics_alarm.hpp: kleine Logikkorrekturen vor Compile ----------
h = HDR.read_text()
h = replace_once(
    h,
    "bool deviceMaxDirty = false;\nbool deviceMaxResetTimestampPending = false;",
    "bool deviceMaxDirty = false;\nbool deviceMaxNvsDirty = false;\nbool deviceMaxResetTimestampPending = false;",
    "NVS dirty flag"
)
h = replace_once(
    h,
    "unsigned long deviceAlarmLastDrainAttemptMs = 0;\nString deviceAlarmLastError = \"\";",
    "unsigned long deviceAlarmLastDrainAttemptMs = 0;\nunsigned long deviceAlarmDrainWaitMs = DEVICE_ALARM_DRAIN_OK_INTERVAL_MS;\nString deviceAlarmLastError = \"\";",
    "alarm drain wait"
)
h = replace_once(
    h,
    "  deviceAlarmSequence = preferences.getULong(\"alarm_seq\", 0UL);\n  deviceLastMaxResetCommandId = preferences.getString(\"cmd_max\", \"\");",
    "  deviceAlarmSequence = preferences.getULong(\"alarm_seq\", 0UL);\n  deviceAlarmActiveMask = (uint16_t)preferences.getUInt(\"alarm_mask\", 0U);\n  deviceLastMaxResetCommandId = preferences.getString(\"cmd_max\", \"\");\n\n  if ((deviceMaxValues.validMask & DEVICE_MAX_SPEED_VALID) && !isfinite(deviceMaxValues.maxSpeed)) deviceMaxValues.validMask &= ~DEVICE_MAX_SPEED_VALID;\n  if ((deviceMaxValues.validMask & DEVICE_MAX_RPM_VALID) && !isfinite(deviceMaxValues.maxRpm)) deviceMaxValues.validMask &= ~DEVICE_MAX_RPM_VALID;\n  if ((deviceMaxValues.validMask & DEVICE_MAX_OIL_TEMP_VALID) && !isfinite(deviceMaxValues.maxOilTemp)) deviceMaxValues.validMask &= ~DEVICE_MAX_OIL_TEMP_VALID;\n  if ((deviceMaxValues.validMask & DEVICE_MAX_CYL_TEMP_VALID) && !isfinite(deviceMaxValues.maxCylTemp)) deviceMaxValues.validMask &= ~DEVICE_MAX_CYL_TEMP_VALID;\n  if ((deviceMaxValues.validMask & DEVICE_MIN_OIL_PRESSURE_VALID) && !isfinite(deviceMaxValues.minOilPressure)) deviceMaxValues.validMask &= ~DEVICE_MIN_OIL_PRESSURE_VALID;\n  if ((deviceMaxValues.validMask & DEVICE_MIN_BATTERY_VALID) && !isfinite(deviceMaxValues.minBattery)) deviceMaxValues.validMask &= ~DEVICE_MIN_BATTERY_VALID;",
    "load validation"
)
h = replace_once(
    h,
    "  if (!preferencesOk || !deviceMaxDirty) return;",
    "  if (!preferencesOk || !deviceMaxNvsDirty) return;",
    "NVS save guard"
)
h = replace_once(
    h,
    "  deviceMaxLastPersistMs = now;\n}",
    "  deviceMaxLastPersistMs = now;\n  deviceMaxNvsDirty = false;\n}",
    "NVS clear dirty"
)
h = replace_once(
    h,
    "  deviceMaxValues.validMask |= bit;\n  deviceMaxDirty = true;",
    "  deviceMaxValues.validMask |= bit;\n  deviceMaxDirty = true;\n  deviceMaxNvsDirty = true;",
    "max changed flags"
)
h = replace_once(
    h,
    "  deviceMaxValues = DeviceMaxValues{};\n  deviceMaxDirty = true;",
    "  deviceMaxValues = DeviceMaxValues{};\n  deviceMaxDirty = true;\n  deviceMaxNvsDirty = true;",
    "reset dirty flags"
)
h = replace_once(
    h,
    "  deviceMaxAktualisieren();\n  deviceMaxSpeichern(true);\n  deviceMaxUploadBearbeiten();",
    "  deviceMaxAktualisieren();\n  deviceMaxSpeichern(true);\n  deviceMaxLastUploadAttemptMs = millis() - DEVICE_MAX_UPLOAD_RETRY_MS;\n  deviceMaxUploadBearbeiten();",
    "reset force upload"
)
h = replace_once(
    h,
    "  const uint16_t newlyActive = current & ~deviceAlarmActiveMask;\n  deviceAlarmActiveMask = current;",
    "  const uint16_t newlyActive = current & ~deviceAlarmActiveMask;\n  if (current != deviceAlarmActiveMask && preferencesOk) {\n    preferences.putUInt(\"alarm_mask\", current);\n  }\n  deviceAlarmActiveMask = current;",
    "persist alarm mask"
)
h = replace_once(
    h,
    "  if ((unsigned long)(now - deviceAlarmLastDrainAttemptMs) < DEVICE_ALARM_DRAIN_OK_INTERVAL_MS) return;",
    "  if ((unsigned long)(now - deviceAlarmLastDrainAttemptMs) < deviceAlarmDrainWaitMs) return;",
    "drain dynamic wait"
)
h = replace_once(
    h,
    "  if (!firebasePut(deviceAlarmFirebasePath(rec), deviceAlarmJson(rec))) {\n    deviceAlarmLastDrainAttemptMs = now +\n      (DEVICE_ALARM_DRAIN_ERROR_BACKOFF_MS - DEVICE_ALARM_DRAIN_OK_INTERVAL_MS);\n    return;\n  }",
    "  if (!firebasePut(deviceAlarmFirebasePath(rec), deviceAlarmJson(rec))) {\n    deviceAlarmDrainWaitMs = DEVICE_ALARM_DRAIN_ERROR_BACKOFF_MS;\n    return;\n  }\n  deviceAlarmDrainWaitMs = DEVICE_ALARM_DRAIN_OK_INTERVAL_MS;",
    "drain backoff"
)
HDR.write_text(h)

# ---------- Firmware core ----------
c = CORE.read_text()
c = replace_once(
    c,
    "MF35X LIVETRACKER V5.9.12 OTA SIGNED - OFFLINE-RENNSPEICHER + NETZUNABHAENGIGE GPIO11-STEUERUNG",
    "MF35X LIVETRACKER V5.9.13 OTA SIGNED - ESP32-MAXWERTE/ALARME + OFFLINE-RENNSPEICHER + NETZUNABHAENGIGE GPIO11-STEUERUNG",
    "core version header"
)
c = replace_once(
    c,
    '#include "offline_race_buffer.hpp"\n',
    '#include "offline_race_buffer.hpp"\n\n// V5.9.13: Maximalwerte und Alarmhistorie werden zentral vom ESP32 erzeugt.\n#include "device_metrics_alarm.hpp"\n',
    "derived include"
)
c = replace_once(
    c,
    "  if (rpmDue) letzterRpmUpload = jetzt;\n  if (gpsDue) letzterGpsUpload = jetzt;\n\n  if (WiFi.status() != WL_CONNECTED) return;",
    "  if (rpmDue) letzterRpmUpload = jetzt;\n  if (gpsDue) letzterGpsUpload = jetzt;\n\n  // V5.9.13: Maximalwerte und Alarmzustand werden lokal und netzunabhaengig ausgewertet.\n  deviceDerivedDataAktualisieren();\n\n  if (WiFi.status() != WL_CONNECTED) return;",
    "derived local update"
)
c = replace_once(
    c,
    "  if (requestId == letzterRebootCommandId && status == \"restarting\") {\n    commandStatusSenden(\"esp32_reboot\", requestId, \"completed\");\n  }\n\n  if (commandAusObjektLesen(commandsJson, \"gps_restart\"",
    "  if (requestId == letzterRebootCommandId && status == \"restarting\") {\n    commandStatusSenden(\"esp32_reboot\", requestId, \"completed\");\n  }\n\n  // V5.9.13: Admin-Reset der Maximalwerte wird vom ESP32 selbst ausgefuehrt.\n  deviceDerivedDataCommandBearbeiten(commandsJson);\n\n  if (commandAusObjektLesen(commandsJson, \"gps_restart\"",
    "max reset command hook"
)
c = replace_once(
    c,
    '  jsonBoolFeld(json, first, "historySupported", true);\n',
    '  jsonBoolFeld(json, first, "historySupported", true);\n  jsonBoolFeld(json, first, "maxValuesDeviceOwned", true);\n  jsonBoolFeld(json, first, "alarmHistoryDeviceOwned", true);\n  jsonBoolFeld(json, first, "alarmHistoryOfflineBufferReady", deviceAlarmQueueReady);\n  jsonULongFeld(json, first, "alarmHistoryOfflinePending", deviceAlarmPendingCount);\n  jsonULongFeld(json, first, "alarmHistoryOfflineQueued", deviceAlarmQueuedCount);\n  jsonULongFeld(json, first, "alarmHistoryOfflineReplayed", deviceAlarmReplayedCount);\n  jsonULongFeld(json, first, "alarmHistoryOfflineDropped", deviceAlarmDroppedCount);\n',
    "device capabilities"
)
c = replace_once(
    c,
    '  Serial.println("MF35X LIVETRACKER V5.9.12 OTA SIGNED");',
    '  Serial.println("MF35X LIVETRACKER V5.9.13 OTA SIGNED");',
    "setup version"
)
c = replace_once(
    c,
    "  offlinePufferInitialisieren();\n\n  // Ausgang sofort sicher LOW.",
    "  offlinePufferInitialisieren();\n\n  // V5.9.13: lokale Maximalwerte + Alarmqueue laden.\n  deviceDerivedDataInitialisieren();\n\n  // Ausgang sofort sicher LOW.",
    "derived init"
)
c = replace_once(
    c,
    "  // Einmal lokale Werte erfassen.\n  alleSensorenEinmalLesen();\n\n  wlanVerbinden();",
    "  // Einmal lokale Werte erfassen.\n  alleSensorenEinmalLesen();\n  deviceDerivedDataAktualisieren();\n\n  wlanVerbinden();",
    "initial derived values"
)
c = replace_once(
    c,
    "    // Beim Start sofort die aktuellen Website-Werte uebernehmen.\n    firebaseKonfigurationLaden(true);\n    deviceStatusSenden();",
    "    // Beim Start sofort die aktuellen Website-Werte uebernehmen.\n    firebaseKonfigurationLaden(true);\n    // Vor dem Device-Status alte browserbasierte Maxwerte einmalig einlesen und zusammenfuehren.\n    deviceDerivedDataBearbeiten();\n    deviceStatusSenden();",
    "initial online merge"
)
c = replace_once(
    c,
    "  // Alle Website-Intervalle werden hier wirksam.\n  liveUpdatesBearbeiten();\n\n  // Rennaufzeichnung nutzt",
    "  // Alle Website-Intervalle werden hier wirksam.\n  liveUpdatesBearbeiten();\n\n  // V5.9.13: NVS-Sicherung, Maxwert-Sync und Alarm-Nachsenden.\n  deviceDerivedDataBearbeiten();\n\n  // Rennaufzeichnung nutzt",
    "derived loop handling"
)
c = replace_once(
    c,
    "  Serial.println(\"--- Offline-Rennpuffer ---\");",
    "  Serial.println(\"--- ESP32-Maximalwerte / Alarmhistorie ---\");\n  Serial.print(\"Datenquelle: ESP32 | Max-NVS: \" );\n  Serial.print(deviceMaxNvsDirty ? \"offen\" : \"gesichert\");\n  Serial.print(\" | Alarm pending/queued/replayed/dropped: \" );\n  Serial.print(deviceAlarmPendingCount);\n  Serial.print('/');\n  Serial.print(deviceAlarmQueuedCount);\n  Serial.print('/');\n  Serial.print(deviceAlarmReplayedCount);\n  Serial.print('/');\n  Serial.println(deviceAlarmDroppedCount);\n  if (deviceAlarmLastError.length() > 0) {\n    Serial.print(\"Alarm-Puffer letzter Fehler: \" );\n    Serial.println(deviceAlarmLastError);\n  }\n\n  Serial.println(\"--- Offline-Rennpuffer ---\");",
    "serial derived status"
)
CORE.write_text(c)

# ---------- Firmware version ----------
v = VER.read_text()
v = replace_once(v, '#define MF35X_FIRMWARE_VERSION "V5.9.12"', '#define MF35X_FIRMWARE_VERSION "V5.9.13"', "version string")
v = replace_once(v, '#define MF35X_FIRMWARE_VERSION_CODE 50912UL', '#define MF35X_FIRMWARE_VERSION_CODE 50913UL', "version code")
VER.write_text(v)

i = INO.read_text()
i = i.replace("V5.9.12 OTA SIGNED", "V5.9.13 OTA SIGNED", 1)
INO.write_text(i)

# ---------- Visitor: nach V5.9.13 rein lesend ----------
s = SCRIPT.read_text()
s = s.replace("/* MF35X Tracker V9.5.11 – Maximalwerte sauber neu erfassen */", "/* MF35X Tracker V9.5.12 – Besucher liest Maximalwerte/Alarmhistorie nur noch */", 1)
s = replace_once(
    s,
    'import { getDatabase, ref, onValue, set, get, runTransaction } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-database.js";',
    'import { getDatabase, ref, onValue } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-database.js";',
    "visitor firebase imports"
)
s = s.replace("let lastMaxResetAt = 0;\n", "", 1)
s = replace_once(
    s,
    'const HMAX = 60;\nconst AMAX = 30;\n',
    'const HMAX = 60;\n',
    "remove AMAX"
)
s = replace_once(
    s,
    'onValue(ref(db, "tracker/commands/resetMaxValues"), s => {\n  let cmd = s.val();\n  if (cmd && cmd.resetAt && cmd.resetAt !== lastMaxResetAt) {\n    lastMaxResetAt = cmd.resetAt;\n    resetMaxFromCurrentLive(cmd.resetAt);\n  }\n});\n\n',
    '',
    "remove visitor reset listener"
)
s = replace_once(
    s,
    '  gpsq(hd, gpsValid);\n  maxv({\n    speed,\n    rpm,\n    oilTemp: ot,\n    cylTemp: ct,\n    oilPressure: op,\n    battery: bat\n  });\n  evaluateAlarms();',
    '  gpsq(hd, gpsValid);\n  evaluateAlarms();',
    "remove visitor max writer call"
)
for fn in ("resetMaxFromCurrentLive", "maxv", "max", "min"):
    s = replace_function(s, fn, "")
s = replace_function(
    s,
    "hist",
    '''function hist(a) {
  const cur = new Set(a.map(x => x.key));
  const neu = a.filter(x => !active.has(x.key));
  active = cur;

  // Historie selbst wird ab V5.9.13 ausschliesslich vom ESP32 geschrieben.
  // Der Browser darf weiterhin lokale Benachrichtigungen anzeigen.
  neu.forEach(x => notify("MF35X Alarm", x.text));
}'''
)
s = replace_function(
    s,
    "renderHist",
    '''function renderHist(raw) {
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
}'''
)
SCRIPT.write_text(s)

# ---------- Admin: Reset an ESP32 delegieren + neue Historie lesen ----------
a = ADMIN.read_text()
a = a.replace("/* MF35X Tracker Admin V9.5.13 */", "/* MF35X Tracker Admin V9.5.14 */", 1)
a = replace_once(
    a,
    "let offlineHistoryPendingCount = 0;\n",
    "let offlineHistoryPendingCount = 0;\nlet maxValuesDeviceOwned = false;\n",
    "admin capability variable"
)
a = replace_once(
    a,
    "    offlineHistoryPendingCount = Number(device.historyOfflinePending || 0);\n",
    "    offlineHistoryPendingCount = Number(device.historyOfflinePending || 0);\n    maxValuesDeviceOwned = device.maxValuesDeviceOwned === true;\n",
    "admin capability read"
)
a = replace_once(
    a,
    '    document.getElementById("restartEsp32").disabled = !systemCommandsSupported;\n',
    '    document.getElementById("restartEsp32").disabled = !systemCommandsSupported;\n    document.getElementById("resetMaxValues").disabled = !systemCommandsSupported || !maxValuesDeviceOwned;\n',
    "admin reset enable"
)
a = a.replace('await set(ref(db, "tracker/alarmHistory"), []);', 'await set(ref(db, "tracker/alarmHistory"), null);', 1)
a = replace_function(
    a,
    "resetMaxValues",
    '''async function resetMaxValues() {
  if (!systemCommandsSupported || !maxValuesDeviceOwned) {
    alert("Maximalwerte werden erst ab der ESP32-Firmware V5.9.13 zentral vom Gerät verwaltet.");
    return;
  }

  await sendSystemCommand(
    "max_values_reset",
    "Maximalwerte zurücksetzen",
    "Maximalwerte wirklich zurücksetzen? Der ESP32 beginnt danach sofort mit einer neuen Erfassung."
  );
}'''
)
a = replace_function(
    a,
    "listenAlarmHistory",
    '''function listenAlarmHistory() {
  onValue(ref(db, "tracker/alarmHistory"), snapshot => {
    const history = normalizeAdminAlarmHistory(snapshot.val());
    const container = document.getElementById("alarmHistory");

    if (!history.length) {
      container.innerHTML = '<div class="empty-history">Noch keine Alarme.</div>';
      return;
    }

    container.innerHTML = history.map(entry => `
      <div class="alarm-entry ${entry.level === "warning" ? "warning-entry" : ""}">
        <div class="alarm-time">${formatAdminAlarmTime(entry)}</div>
        <div class="alarm-message">${entry.text || "Alarm"}</div>
      </div>
    `).join("");
  });
}

function normalizeAdminAlarmHistory(raw) {
  let history = [];
  if (Array.isArray(raw)) {
    history = raw.filter(Boolean);
  } else if (raw && typeof raw === "object") {
    history = Object.values(raw).filter(x => x && typeof x === "object");
  }

  return history
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

function formatAdminAlarmTime(entry) {
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
}'''
)
ADMIN.write_text(a)

# ---------- Sanity checks ----------
core = CORE.read_text()
script = SCRIPT.read_text()
admin = ADMIN.read_text()
header = HDR.read_text()

checks = {
    "core V5.9.13": "V5.9.13" in core,
    "derived include": 'device_metrics_alarm.hpp' in core,
    "local derived update": "deviceDerivedDataAktualisieren();" in core,
    "loop derived handling": "deviceDerivedDataBearbeiten();" in core,
    "max reset command": "deviceDerivedDataCommandBearbeiten(commandsJson);" in core,
    "visitor no runTransaction": "runTransaction" not in script,
    "visitor no maxv": "maxv(" not in script,
    "visitor no alarmHistory set": 'set(ref(db, "tracker/alarmHistory")' not in script,
    "admin device reset": '"max_values_reset"' in admin,
    "alarm queue": "DeviceAlarmRecord" in header,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("Sanity-Check fehlgeschlagen: " + ", ".join(failed))

print("Point-5-Patch erfolgreich angewendet.")
