from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HDR = ROOT / "esp32/MF35X_Livetracker/device_metrics_alarm.hpp"
ADMIN = ROOT / "admin.js"


def rep(text, old, new, label):
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"{label}: erwartet 1 Treffer, gefunden {n}")
    return text.replace(old, new, 1)

h = HDR.read_text()
h = rep(
    h,
    'String deviceLastMaxResetCommandId = "";\n',
    'String deviceLastMaxResetCommandId = "";\nString deviceLastAlarmClearCommandId = "";\n',
    'alarm clear command id'
)
h = rep(
    h,
    '  deviceLastMaxResetCommandId = preferences.getString("cmd_max", "");\n',
    '  deviceLastMaxResetCommandId = preferences.getString("cmd_max", "");\n  deviceLastAlarmClearCommandId = preferences.getString("cmd_ahclr", "");\n',
    'load alarm clear command id'
)
old_tail = '''void deviceDerivedDataCommandBearbeiten(const String& commandsJson) {
  String requestId;
  String status;

  if (!commandAusObjektLesen(commandsJson, "max_values_reset", requestId, status)) return;
  if (requestId == deviceLastMaxResetCommandId) return;

  deviceLastMaxResetCommandId = requestId;
  if (preferencesOk) preferences.putString("cmd_max", requestId);

  commandStatusSenden("max_values_reset", requestId, "resetting");
  deviceMaxReset();
  commandStatusSenden("max_values_reset", requestId, "completed", "Maximalwerte vom ESP32 neu gestartet.");
}
'''
new_tail = '''bool deviceAlarmHistorieLeeren() {
  // Erst den lokalen Rueckstau entfernen. Damit koennen geloeschte Alarme
  // spaeter nicht erneut aus LittleFS nach Firebase zurueckkehren.
  if (deviceAlarmQueueReady) {
    LittleFS.remove(DEVICE_ALARM_QUEUE_PATH);
    LittleFS.remove(DEVICE_ALARM_QUEUE_REPAIR_PATH);
    deviceAlarmPendingCount = 0;
    deviceAlarmLastError = "";
    offlineFsStatusAktualisieren();
  }

  // Aktive Alarmzustaende bleiben bewusst erhalten. Ein gerade anstehender
  // Alarm wird durch "Historie leeren" nicht kuenstlich als neues Ereignis erzeugt.
  return WiFi.status() == WL_CONNECTED &&
         firebasePut("tracker/alarmHistory", "null");
}

void deviceDerivedDataCommandBearbeiten(const String& commandsJson) {
  String requestId;
  String status;

  if (commandAusObjektLesen(commandsJson, "max_values_reset", requestId, status) &&
      requestId != deviceLastMaxResetCommandId) {
    deviceLastMaxResetCommandId = requestId;
    if (preferencesOk) preferences.putString("cmd_max", requestId);

    commandStatusSenden("max_values_reset", requestId, "resetting");
    deviceMaxReset();
    commandStatusSenden(
      "max_values_reset",
      requestId,
      "completed",
      "Maximalwerte vom ESP32 neu gestartet."
    );
  }

  if (commandAusObjektLesen(commandsJson, "alarm_history_clear", requestId, status) &&
      requestId != deviceLastAlarmClearCommandId) {
    deviceLastAlarmClearCommandId = requestId;
    if (preferencesOk) preferences.putString("cmd_ahclr", requestId);

    commandStatusSenden("alarm_history_clear", requestId, "clearing");
    if (deviceAlarmHistorieLeeren()) {
      commandStatusSenden(
        "alarm_history_clear",
        requestId,
        "completed",
        "Alarmhistorie und lokaler Alarm-Rueckstau geloescht."
      );
    } else {
      commandStatusSenden(
        "alarm_history_clear",
        requestId,
        "error",
        "Alarmhistorie konnte nicht vollstaendig geloescht werden."
      );
    }
  }
}
'''
h = rep(h, old_tail, new_tail, 'derived command handler')
HDR.write_text(h)

a = ADMIN.read_text()
a = rep(
    a,
    'let maxValuesDeviceOwned = false;\n',
    'let maxValuesDeviceOwned = false;\nlet alarmHistoryDeviceOwned = false;\n',
    'admin alarm ownership variable'
)
a = rep(
    a,
    '''  document.getElementById("clearAlarmHistory").addEventListener("click", async () => {
    await set(ref(db, "tracker/alarmHistory"), null);
    alert("Alarmhistorie geleert.");
  });''',
    '''  document.getElementById("clearAlarmHistory").addEventListener("click", clearAlarmHistory);''',
    'admin clear handler'
)
a = rep(
    a,
    '    maxValuesDeviceOwned = device.maxValuesDeviceOwned === true;\n',
    '    maxValuesDeviceOwned = device.maxValuesDeviceOwned === true;\n    alarmHistoryDeviceOwned = device.alarmHistoryDeviceOwned === true;\n',
    'admin alarm ownership read'
)
a = rep(
    a,
    '    document.getElementById("resetMaxValues").disabled = !systemCommandsSupported || !maxValuesDeviceOwned;\n',
    '    document.getElementById("resetMaxValues").disabled = !systemCommandsSupported || !maxValuesDeviceOwned;\n    document.getElementById("clearAlarmHistory").disabled = !systemCommandsSupported || !alarmHistoryDeviceOwned;\n',
    'admin clear button state'
)
a = rep(
    a,
    '["checking", "downloading", "verifying", "restarting"].includes(state.status)',
    '["checking", "downloading", "verifying", "restarting", "resetting", "clearing"].includes(state.status)',
    'admin pending command states'
)
needle = '''async function resetMaxValues() {
  if (!systemCommandsSupported || !maxValuesDeviceOwned) {
    alert("Maximalwerte werden erst ab der ESP32-Firmware V5.9.13 zentral vom Gerät verwaltet.");
    return;
  }

  await sendSystemCommand(
    "max_values_reset",
    "Maximalwerte zurücksetzen",
    "Maximalwerte wirklich zurücksetzen? Der ESP32 beginnt danach sofort mit einer neuen Erfassung."
  );
}
'''
replacement = needle + '''
async function clearAlarmHistory() {
  if (!systemCommandsSupported || !alarmHistoryDeviceOwned) {
    alert("Die Alarmhistorie wird erst ab der ESP32-Firmware V5.9.13 zentral vom Gerät verwaltet.");
    return;
  }

  await sendSystemCommand(
    "alarm_history_clear",
    "Alarmhistorie leeren",
    "Alarmhistorie wirklich leeren? Noch lokal gepufferte Alarmereignisse werden dabei ebenfalls gelöscht."
  );
}
'''
a = rep(a, needle, replacement, 'admin clear function')
ADMIN.write_text(a)

print('Point-5-Abschlusskorrektur angewendet.')
