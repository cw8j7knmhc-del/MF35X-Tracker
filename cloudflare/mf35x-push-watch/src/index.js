/*
 * MF35X Web Push – Mehrgeräte + schnelle Sensoralarme
 *
 * Isolation:
 * - liest nur tracker/live und tracker/settings aus Firebase RTDB
 * - schreibt NICHT in Firebase
 * - greift NICHT auf ESP32, GPIO11, Rennaufzeichnung oder OTA zu
 * - registrierte Push-Geraete und Zustandsmerker liegen nur in Cloudflare KV
 * - FCM/Google-Schluessel bleiben Cloudflare-Secrets
 */

const DEFAULT_OFFLINE_AFTER_MS = 45000;
const SENSOR_LIVE_MAX_AGE_MS = 15000;
const SENSOR_POLL_INTERVAL_MS = 10000;
const SENSOR_POLLS_PER_CRON = 6;
const OIL_PRESSURE_RPM_MIN = 400;
const OIL_PRESSURE_START_DELAY_MS = 5000;

const ESP_STATE_KEY = "mf35x:esp32-online-state";
const SENSOR_STATE_KEY = "mf35x:sensor-state-v1";
const DEVICE_PREFIX = "mf35x:push-device:";

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

const DEFAULT_NOTIFICATIONS = {
  espStatus: true,
  battery: true,
  oilPressure: true,
  oilTemp: true,
  cylinderTemp: true
};

export default {
  async scheduled(_controller, env, _ctx) {
    // Online/Offline bleibt bewusst der einmal-pro-Minute-Waechter.
    await checkEspStatus(env);

    // Grenzwerte einmal pro Minute laden. Sensor-Livewerte werden innerhalb
    // derselben Cron-Ausfuehrung alle 10 Sekunden geprueft.
    const limits = await readLimits(env);

    for (let i = 0; i < SENSOR_POLLS_PER_CRON; i++) {
      await checkSensorAlarms(env, limits);

      if (i + 1 < SENSOR_POLLS_PER_CRON) {
        await scheduler.wait(SENSOR_POLL_INTERVAL_MS);
      }
    }
  },

  async fetch(request, env) {
    const url = new URL(request.url);

    if (request.method === "OPTIONS") {
      return corsResponse(null, 204);
    }

    if (url.pathname === "/health") {
      try {
        const status = await readEspStatus(env);
        const remembered = await env.STATE.get(ESP_STATE_KEY);
        const devices = await listDevices(env);

        return corsJson({
          ok: true,
          currentState: status.online ? "online" : "offline",
          rememberedState: remembered || null,
          liveTimestamp: status.liveTimestamp,
          ageMs: status.ageMs,
          offlineAfterMs: status.offlineAfterMs,
          registeredDevices: devices.length
        });
      } catch (error) {
        return corsJson({
          ok: false,
          error: String(error?.message || error)
        }, 500);
      }
    }

    if (url.pathname.startsWith("/api/devices/")) {
      if (request.method !== "POST") {
        return corsJson({ ok: false, error: "POST erforderlich." }, 405);
      }

      try {
        const body = await readJsonBody(request);
        requireAdminCode(env, body.code);

        switch (url.pathname) {
          case "/api/devices/list":
            return corsJson({
              ok: true,
              devices: sanitizeDevices(await listDevices(env))
            });

          case "/api/devices/register": {
            const device = await registerDevice(env, body);
            return corsJson({
              ok: true,
              device: sanitizeDevice(device)
            });
          }

          case "/api/devices/update": {
            const device = await updateDevice(env, body);
            return corsJson({
              ok: true,
              device: sanitizeDevice(device)
            });
          }

          case "/api/devices/delete":
            await deleteDevice(env, body.id);
            return corsJson({ ok: true });

          case "/api/devices/test":
            await testDevice(env, body.id);
            return corsJson({ ok: true });

          default:
            return corsJson({ ok: false, error: "Unbekannter API-Pfad." }, 404);
        }
      } catch (error) {
        const status = String(error?.message || "").includes("Verwaltungscode")
          ? 403
          : 400;

        return corsJson({
          ok: false,
          error: String(error?.message || error)
        }, status);
      }
    }

    return new Response("MF35X Push Watch – OK", {
      status: 200,
      headers: {
        "content-type": "text/plain; charset=utf-8",
        ...corsHeaders()
      }
    });
  }
};

/* ============================================================
 * ESP32 Online / Offline
 * ============================================================ */

async function checkEspStatus(env) {
  const status = await readEspStatus(env);
  const currentState = status.online ? "online" : "offline";
  const previousState = await env.STATE.get(ESP_STATE_KEY);

  if (previousState !== "online" && previousState !== "offline") {
    await env.STATE.put(ESP_STATE_KEY, currentState);
    console.log(`Initial ESP32 state stored: ${currentState}`);
    return;
  }

  if (previousState === currentState) return;

  const notification = currentState === "online"
    ? {
        title: "MF35X Tracker",
        body: "ESP32 ist wieder online – neue Tracker-Daten kommen wieder an."
      }
    : {
        title: "MF35X Tracker",
        body: "ESP32 ist offline – seit mindestens 45 Sekunden keine neuen Tracker-Daten."
      };

  await sendEventToDevices(
    env,
    "espStatus",
    notification.title,
    notification.body,
    "mf35x-esp32-status",
    { mf35xState: currentState }
  );

  await env.STATE.put(ESP_STATE_KEY, currentState);
  console.log(`ESP32 state changed ${previousState} -> ${currentState}.`);
}

async function readEspStatus(env) {
  const firebaseRoot = firebaseRootUrl(env);
  const offlineAfterMs = parsePositiveInteger(
    env.OFFLINE_AFTER_MS,
    DEFAULT_OFFLINE_AFTER_MS
  );

  const response = await fetchJson(
    `${firebaseRoot}/tracker/live/timestamp.json`
  );

  const liveTimestamp = numberValue(response);
  const now = Date.now();

  if (liveTimestamp == null || liveTimestamp <= 0) {
    return {
      online: false,
      liveTimestamp: null,
      ageMs: null,
      offlineAfterMs
    };
  }

  const ageMs = Math.max(0, now - liveTimestamp);
  return {
    online: ageMs <= offlineAfterMs,
    liveTimestamp,
    ageMs,
    offlineAfterMs
  };
}

/* ============================================================
 * Sensoralarme – 10-Sekunden-Pruefung ohne ESP32-Aenderung
 * ============================================================ */

async function readLimits(env) {
  try {
    const raw = await fetchJson(
      `${firebaseRootUrl(env)}/tracker/settings.json`
    );

    return {
      batteryWarn: safeLimit(raw?.batteryWarn, DEFAULT_LIMITS.batteryWarn),
      batteryAlarm: safeLimit(raw?.batteryAlarm, DEFAULT_LIMITS.batteryAlarm),
      oilPressureWarn: safeLimit(raw?.oilPressureWarn, DEFAULT_LIMITS.oilPressureWarn),
      oilPressureAlarm: safeLimit(raw?.oilPressureAlarm, DEFAULT_LIMITS.oilPressureAlarm),
      oilTempWarn: safeLimit(raw?.oilTempWarn, DEFAULT_LIMITS.oilTempWarn),
      oilTempAlarm: safeLimit(raw?.oilTempAlarm, DEFAULT_LIMITS.oilTempAlarm),
      cylTempWarn: safeLimit(raw?.cylTempWarn, DEFAULT_LIMITS.cylTempWarn),
      cylTempAlarm: safeLimit(raw?.cylTempAlarm, DEFAULT_LIMITS.cylTempAlarm)
    };
  } catch (error) {
    console.warn("Grenzwerte konnten nicht geladen werden; Standards werden verwendet:", error);
    return { ...DEFAULT_LIMITS };
  }
}

async function checkSensorAlarms(env, limits) {
  let live;
  try {
    live = await fetchJson(
      `${firebaseRootUrl(env)}/tracker/live.json`
    );
  } catch (error) {
    console.warn("Sensor-Livewerte konnten nicht gelesen werden:", error);
    return;
  }

  if (!live || typeof live !== "object") return;

  const timestamp = numberValue(live.timestamp);
  const now = Date.now();

  // Keine Alarme aus alten/stalen Daten erzeugen.
  if (
    timestamp == null ||
    timestamp <= 0 ||
    now - timestamp > SENSOR_LIVE_MAX_AGE_MS
  ) {
    return;
  }

  const oldState = await readSensorState(env);
  const newState = {
    ...oldState,
    updatedAt: now
  };

  const rpm = numberValue(live.rpm);
  const battery = numberValue(live.battery_v);
  const oilPressure = numberValue(live.oil_pressure);
  const oilTemp = numberValue(live.oil_temp);
  const cylinderTemp = numberValue(live.cylinder_temp);

  await evaluateAndNotify(
    env,
    "battery",
    oldState.battery,
    classifyLow(battery, limits.batteryWarn, limits.batteryAlarm),
    battery,
    "V",
    "Batteriespannung",
    newState
  );

  await evaluateAndNotify(
    env,
    "oilTemp",
    oldState.oilTemp,
    classifyHigh(oilTemp, limits.oilTempWarn, limits.oilTempAlarm),
    oilTemp,
    "°C",
    "Öltemperatur",
    newState
  );

  await evaluateAndNotify(
    env,
    "cylinderTemp",
    oldState.cylinderTemp,
    classifyHigh(cylinderTemp, limits.cylTempWarn, limits.cylTempAlarm),
    cylinderTemp,
    "°C",
    "Zylinderkopftemperatur",
    newState
  );

  const engineRunning = rpm != null && rpm >= OIL_PRESSURE_RPM_MIN;

  if (!engineRunning) {
    newState.engineRunSinceMs = 0;
    // Motor aus = Öldruckbewertung deaktiviert. Kein "wieder OK"-Push erzeugen.
    newState.oilPressure = "inactive";
  } else {
    let engineRunSince = numberValue(oldState.engineRunSinceMs) || 0;
    if (!engineRunSince) engineRunSince = now;
    newState.engineRunSinceMs = engineRunSince;

    if (now - engineRunSince >= OIL_PRESSURE_START_DELAY_MS) {
      await evaluateAndNotify(
        env,
        "oilPressure",
        oldState.oilPressure,
        classifyLow(
          oilPressure,
          limits.oilPressureWarn,
          limits.oilPressureAlarm
        ),
        oilPressure,
        "bar",
        "Öldruck",
        newState,
        { suppressRecoveryFrom: "inactive" }
      );
    } else {
      newState.oilPressure = "inactive";
    }
  }

  await env.STATE.put(SENSOR_STATE_KEY, JSON.stringify(newState));
}

async function evaluateAndNotify(
  env,
  category,
  previousState,
  currentState,
  value,
  unit,
  label,
  stateObject,
  options = {}
) {
  stateObject[category] = currentState;

  if (!currentState || currentState === "unknown") return;
  if (!previousState || previousState === "unknown") return;
  if (previousState === currentState) return;

  if (
    currentState === "ok" &&
    options.suppressRecoveryFrom &&
    previousState === options.suppressRecoveryFrom
  ) {
    return;
  }

  const formatted = formatSensorValue(value);

  if (currentState === "alarm") {
    await sendEventToDevices(
      env,
      category,
      `MF35X ALARM – ${label}`,
      `${label} kritisch: ${formatted} ${unit}`,
      `mf35x-${category}`,
      { category, level: "alarm" }
    );
    return;
  }

  if (currentState === "warning") {
    await sendEventToDevices(
      env,
      category,
      `MF35X Warnung – ${label}`,
      `${label} im Warnbereich: ${formatted} ${unit}`,
      `mf35x-${category}`,
      { category, level: "warning" }
    );
    return;
  }

  if (
    currentState === "ok" &&
    (previousState === "warning" || previousState === "alarm")
  ) {
    await sendEventToDevices(
      env,
      category,
      `MF35X – ${label} wieder OK`,
      `${label} wieder im Normalbereich: ${formatted} ${unit}`,
      `mf35x-${category}`,
      { category, level: "ok" }
    );
  }
}

function classifyLow(value, warn, alarm) {
  if (value == null) return "unknown";
  if (value <= alarm) return "alarm";
  if (value <= warn) return "warning";
  return "ok";
}

function classifyHigh(value, warn, alarm) {
  if (value == null) return "unknown";
  if (value >= alarm) return "alarm";
  if (value >= warn) return "warning";
  return "ok";
}

async function readSensorState(env) {
  const raw = await env.STATE.get(SENSOR_STATE_KEY);
  if (!raw) {
    return {
      battery: "unknown",
      oilPressure: "inactive",
      oilTemp: "unknown",
      cylinderTemp: "unknown",
      engineRunSinceMs: 0,
      updatedAt: 0
    };
  }

  try {
    return {
      battery: "unknown",
      oilPressure: "inactive",
      oilTemp: "unknown",
      cylinderTemp: "unknown",
      engineRunSinceMs: 0,
      updatedAt: 0,
      ...JSON.parse(raw)
    };
  } catch {
    return {
      battery: "unknown",
      oilPressure: "inactive",
      oilTemp: "unknown",
      cylinderTemp: "unknown",
      engineRunSinceMs: 0,
      updatedAt: 0
    };
  }
}

/* ============================================================
 * Push-Geraete / Admin-API
 * ============================================================ */

async function registerDevice(env, body) {
  const token = String(body.token || "").trim();
  if (token.length < 20) throw new Error("FCM-Token fehlt oder ist ungueltig.");

  const id = await deviceIdForToken(token);
  const key = DEVICE_PREFIX + id;
  const existing = await readDeviceByKey(env, key);

  const now = Date.now();
  const device = {
    id,
    name: cleanName(body.name, existing?.name || "Push-Gerät"),
    platform: cleanPlatform(body.platform, existing?.platform || "Browser"),
    token,
    enabled: existing?.enabled !== false,
    notifications: {
      ...DEFAULT_NOTIFICATIONS,
      ...(existing?.notifications || {})
    },
    createdAt: existing?.createdAt || now,
    updatedAt: now
  };

  await env.STATE.put(key, JSON.stringify(device));
  return device;
}

async function updateDevice(env, body) {
  const id = cleanId(body.id);
  const key = DEVICE_PREFIX + id;
  const existing = await readDeviceByKey(env, key);

  if (!existing) throw new Error("Push-Gerät wurde nicht gefunden.");

  const notifications = {
    ...DEFAULT_NOTIFICATIONS,
    ...(existing.notifications || {})
  };

  if (body.notifications && typeof body.notifications === "object") {
    for (const keyName of Object.keys(DEFAULT_NOTIFICATIONS)) {
      if (typeof body.notifications[keyName] === "boolean") {
        notifications[keyName] = body.notifications[keyName];
      }
    }
  }

  const updated = {
    ...existing,
    enabled: typeof body.enabled === "boolean" ? body.enabled : existing.enabled !== false,
    notifications,
    updatedAt: Date.now()
  };

  if (body.name != null) {
    updated.name = cleanName(body.name, existing.name || "Push-Gerät");
  }

  await env.STATE.put(key, JSON.stringify(updated));
  return updated;
}

async function deleteDevice(env, rawId) {
  const id = cleanId(rawId);
  const key = DEVICE_PREFIX + id;
  const existing = await env.STATE.get(key);
  if (!existing) throw new Error("Push-Gerät wurde nicht gefunden.");
  await env.STATE.delete(key);
}

async function testDevice(env, rawId) {
  const id = cleanId(rawId);
  const device = await readDeviceByKey(env, DEVICE_PREFIX + id);

  if (!device) throw new Error("Push-Gerät wurde nicht gefunden.");

  await sendFcmToToken(
    env,
    device.token,
    "MF35X Push-Test",
    `Push funktioniert auf „${device.name || "diesem Gerät"}“.`,
    "mf35x-device-test",
    { category: "test", level: "test" }
  );
}

async function listDevices(env) {
  const listing = await env.STATE.list({ prefix: DEVICE_PREFIX });
  const devices = [];

  for (const key of listing.keys || []) {
    const device = await readDeviceByKey(env, key.name);
    if (device) devices.push(device);
  }

  devices.sort((a, b) =>
    String(a.name || "").localeCompare(String(b.name || ""), "de")
  );

  return devices;
}

async function readDeviceByKey(env, key) {
  const raw = await env.STATE.get(key);
  if (!raw) return null;

  try {
    const device = JSON.parse(raw);
    if (!device?.id || !device?.token) return null;
    return device;
  } catch {
    return null;
  }
}

function sanitizeDevices(devices) {
  return devices.map(sanitizeDevice);
}

function sanitizeDevice(device) {
  return {
    id: device.id,
    name: device.name || "Push-Gerät",
    platform: device.platform || "Browser",
    enabled: device.enabled !== false,
    notifications: {
      ...DEFAULT_NOTIFICATIONS,
      ...(device.notifications || {})
    },
    tokenHint: shortToken(device.token),
    createdAt: device.createdAt || 0,
    updatedAt: device.updatedAt || 0
  };
}

async function deviceIdForToken(token) {
  const digest = await crypto.subtle.digest(
    "SHA-256",
    new TextEncoder().encode(token)
  );

  return Array.from(new Uint8Array(digest))
    .slice(0, 12)
    .map(byte => byte.toString(16).padStart(2, "0"))
    .join("");
}

function requireAdminCode(env, supplied) {
  const expected = String(env.PUSH_ADMIN_CODE || "");
  const actual = String(supplied || "");

  if (!expected || actual !== expected) {
    throw new Error("Push-Verwaltungscode ist falsch oder nicht konfiguriert.");
  }
}

/* ============================================================
 * Versand
 * ============================================================ */

async function sendEventToDevices(
  env,
  category,
  title,
  body,
  tag,
  data = {}
) {
  const devices = await listDevices(env);
  const recipients = new Map();

  for (const device of devices) {
    if (device.enabled === false) continue;

    const prefs = {
      ...DEFAULT_NOTIFICATIONS,
      ...(device.notifications || {})
    };

    if (prefs[category] === false) continue;
    recipients.set(device.token, device);
  }

  // Legacy-Einzel-Token nur verwenden, solange noch KEIN Gerät in der
  // Mehrgeräteverwaltung registriert ist. Sobald mindestens ein Gerät existiert,
  // gelten ausschließlich dessen Master-Schalter und Benachrichtigungskategorien.
  const legacyToken = String(env.FCM_TOKEN || "").trim();
  if (devices.length === 0 && legacyToken) {
    recipients.set(legacyToken, {
      name: "Legacy-FCM-Empfänger",
      token: legacyToken
    });
  }

  if (!recipients.size) {
    console.log(`Kein aktiver Empfaenger fuer ${category}.`);
    return;
  }

  let successCount = 0;
  const errors = [];

  for (const device of recipients.values()) {
    try {
      await sendFcmToToken(
        env,
        device.token,
        title,
        body,
        tag,
        {
          ...data,
          category,
          title,
          body,
          tag
        }
      );
      successCount++;
    } catch (error) {
      console.error(`Push an ${device.name || "Gerät"} fehlgeschlagen:`, error);
      errors.push(String(error?.message || error));
    }
  }

  if (!successCount && errors.length) {
    throw new Error(`Alle Push-Zustellungen fehlgeschlagen: ${errors[0]}`);
  }
}

async function sendFcmToToken(
  env,
  fcmToken,
  title,
  body,
  tag,
  data = {}
) {
  const token = String(fcmToken || "").trim();
  if (!token) throw new Error("FCM-Token fehlt.");

  const serviceAccount = parseServiceAccount(env.GOOGLE_SERVICE_ACCOUNT_JSON);
  const accessToken = await getGoogleAccessToken(serviceAccount);

  const projectId =
    String(env.FIREBASE_PROJECT_ID || "").trim() ||
    String(serviceAccount.project_id || "").trim();

  if (!projectId) throw new Error("Firebase project_id fehlt.");

  const message = {
    message: {
      token,
      notification: { title, body },
      data: objectToStringValues({
        title,
        body,
        tag,
        ...data
      }),
      webpush: {
        headers: { Urgency: "high" },
        notification: {
          tag: tag || "mf35x-push",
          renotify: true
        }
      }
    }
  };

  const appUrl = String(env.APP_URL || "").trim();
  if (appUrl.startsWith("https://")) {
    message.message.webpush.fcm_options = { link: appUrl };
  }

  const response = await fetch(
    `https://fcm.googleapis.com/v1/projects/${encodeURIComponent(projectId)}/messages:send`,
    {
      method: "POST",
      headers: {
        authorization: `Bearer ${accessToken}`,
        "content-type": "application/json"
      },
      body: JSON.stringify(message)
    }
  );

  if (!response.ok) {
    const text = await response.text();
    throw new Error(`FCM HTTP ${response.status}: ${text.slice(0, 500)}`);
  }
}

/* ============================================================
 * Google OAuth
 * ============================================================ */

function parseServiceAccount(raw) {
  if (!raw) throw new Error("GOOGLE_SERVICE_ACCOUNT_JSON fehlt.");

  let account;
  try {
    account = JSON.parse(String(raw));
  } catch {
    throw new Error("GOOGLE_SERVICE_ACCOUNT_JSON ist kein gueltiges JSON.");
  }

  if (!account.client_email || !account.private_key) {
    throw new Error("Service-Account enthaelt client_email/private_key nicht.");
  }

  return account;
}

async function getGoogleAccessToken(serviceAccount) {
  const now = Math.floor(Date.now() / 1000);

  const header = {
    alg: "RS256",
    typ: "JWT"
  };

  const claims = {
    iss: serviceAccount.client_email,
    scope: "https://www.googleapis.com/auth/firebase.messaging",
    aud: "https://oauth2.googleapis.com/token",
    iat: now,
    exp: now + 3600
  };

  const signingInput =
    `${base64UrlJson(header)}.${base64UrlJson(claims)}`;

  const privateKey = await importPrivateKey(serviceAccount.private_key);

  const signature = await crypto.subtle.sign(
    "RSASSA-PKCS1-v1_5",
    privateKey,
    new TextEncoder().encode(signingInput)
  );

  const assertion =
    `${signingInput}.${base64UrlBytes(new Uint8Array(signature))}`;

  const requestBody = new URLSearchParams({
    grant_type: "urn:ietf:params:oauth:grant-type:jwt-bearer",
    assertion
  });

  const response = await fetch(
    "https://oauth2.googleapis.com/token",
    {
      method: "POST",
      headers: {
        "content-type": "application/x-www-form-urlencoded"
      },
      body: requestBody
    }
  );

  if (!response.ok) {
    const text = await response.text();
    throw new Error(
      `Google OAuth HTTP ${response.status}: ${text.slice(0, 500)}`
    );
  }

  const token = await response.json();
  if (!token.access_token) {
    throw new Error("Google OAuth lieferte kein access_token.");
  }

  return token.access_token;
}

async function importPrivateKey(pem) {
  const normalized = String(pem)
    .replace(/\\n/g, "\n")
    .replace("-----BEGIN PRIVATE KEY-----", "")
    .replace("-----END PRIVATE KEY-----", "")
    .replace(/\s+/g, "");

  const der = base64ToBytes(normalized);

  return crypto.subtle.importKey(
    "pkcs8",
    der.buffer,
    {
      name: "RSASSA-PKCS1-v1_5",
      hash: "SHA-256"
    },
    false,
    ["sign"]
  );
}

/* ============================================================
 * Hilfsfunktionen
 * ============================================================ */

function firebaseRootUrl(env) {
  const root = String(env.FIREBASE_DB_URL || "").replace(/\/+$/, "");
  if (!root.startsWith("https://")) {
    throw new Error("FIREBASE_DB_URL fehlt oder ist ungueltig.");
  }
  return root;
}

async function fetchJson(url) {
  const response = await fetch(url, {
    method: "GET",
    headers: { accept: "application/json" }
  });

  if (!response.ok) {
    throw new Error(`Firebase HTTP ${response.status}`);
  }

  return response.json();
}

async function readJsonBody(request) {
  try {
    const body = await request.json();
    if (!body || typeof body !== "object") {
      throw new Error("JSON-Body fehlt.");
    }
    return body;
  } catch (error) {
    if (String(error?.message || "").includes("JSON-Body")) throw error;
    throw new Error("Ungueltiger JSON-Body.");
  }
}

function corsHeaders() {
  return {
    "access-control-allow-origin": "*",
    "access-control-allow-methods": "GET,POST,OPTIONS",
    "access-control-allow-headers": "content-type",
    "access-control-max-age": "86400"
  };
}

function corsJson(value, status = 200) {
  return new Response(JSON.stringify(value, null, 2), {
    status,
    headers: {
      "content-type": "application/json; charset=utf-8",
      "cache-control": "no-store",
      ...corsHeaders()
    }
  });
}

function corsResponse(body, status = 200) {
  return new Response(body, {
    status,
    headers: corsHeaders()
  });
}

function cleanName(value, fallback) {
  const text = String(value || "").trim().slice(0, 40);
  return text || fallback;
}

function cleanPlatform(value, fallback) {
  const text = String(value || "").trim().slice(0, 60);
  return text || fallback;
}

function cleanId(value) {
  const id = String(value || "").trim();
  if (!/^[a-f0-9]{24}$/.test(id)) {
    throw new Error("Ungueltige Geraete-ID.");
  }
  return id;
}

function shortToken(token) {
  const text = String(token || "");
  if (text.length < 24) return "vorhanden";
  return `${text.slice(0, 8)}…${text.slice(-6)}`;
}

function safeLimit(value, fallback) {
  const number = numberValue(value);
  return number == null ? fallback : number;
}

function numberValue(value) {
  if (value === undefined || value === null || value === "") return null;
  const number = Number(value);
  return Number.isFinite(number) ? number : null;
}

function parsePositiveInteger(value, fallback) {
  const number = Number(value);
  return Number.isFinite(number) && number > 0
    ? Math.floor(number)
    : fallback;
}

function formatSensorValue(value) {
  const number = numberValue(value);
  if (number == null) return "---";
  if (Math.abs(number) >= 100) return String(Math.round(number));
  return number.toFixed(1);
}

function objectToStringValues(value) {
  const result = {};
  for (const [key, item] of Object.entries(value || {})) {
    if (item === undefined || item === null) continue;
    result[key] = String(item);
  }
  return result;
}

function base64UrlJson(value) {
  return base64UrlBytes(
    new TextEncoder().encode(JSON.stringify(value))
  );
}

function base64UrlBytes(bytes) {
  let binary = "";
  const chunk = 0x8000;

  for (let i = 0; i < bytes.length; i += chunk) {
    binary += String.fromCharCode(...bytes.subarray(i, i + chunk));
  }

  return btoa(binary)
    .replace(/\+/g, "-")
    .replace(/\//g, "_")
    .replace(/=+$/g, "");
}

function base64ToBytes(base64) {
  const binary = atob(base64);
  const bytes = new Uint8Array(binary.length);

  for (let i = 0; i < binary.length; i++) {
    bytes[i] = binary.charCodeAt(i);
  }

  return bytes;
}
