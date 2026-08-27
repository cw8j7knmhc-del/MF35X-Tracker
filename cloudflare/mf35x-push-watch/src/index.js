/*
 * MF35X Web Push – Stufe B
 * Externer ESP32 Online/Offline-Waechter fuer Cloudflare Workers.
 *
 * Sicherheit / Isolation:
 * - liest ausschliesslich tracker/live/timestamp aus Firebase RTDB
 * - schreibt NICHT in Firebase
 * - greift NICHT auf ESP32, GPIO11, Rennaufzeichnung oder OTA zu
 * - sendet nur bei echtem Zustandswechsel eine FCM-Nachricht
 * - Status wird in Cloudflare KV gespeichert
 */

const DEFAULT_OFFLINE_AFTER_MS = 45000;
const STATE_KEY = "mf35x:esp32-online-state";

export default {
  async scheduled(_controller, env, _ctx) {
    await checkAndNotify(env);
  },

  async fetch(request, env) {
    const url = new URL(request.url);

    if (url.pathname === "/health") {
      try {
        const status = await readCurrentStatus(env);
        const remembered = await env.STATE.get(STATE_KEY);
        return jsonResponse({
          ok: true,
          currentState: status.online ? "online" : "offline",
          rememberedState: remembered || null,
          liveTimestamp: status.liveTimestamp,
          ageMs: status.ageMs,
          offlineAfterMs: status.offlineAfterMs
        });
      } catch (error) {
        return jsonResponse({
          ok: false,
          error: String(error?.message || error)
        }, 500);
      }
    }

    return new Response("MF35X Push Watch – OK", {
      status: 200,
      headers: { "content-type": "text/plain; charset=utf-8" }
    });
  }
};

async function checkAndNotify(env) {
  const status = await readCurrentStatus(env);
  const currentState = status.online ? "online" : "offline";
  const previousState = await env.STATE.get(STATE_KEY);

  // Beim allerersten Lauf nur Ausgangszustand merken.
  // Dadurch entsteht beim Deployment keine unerwuenschte Startmeldung.
  if (previousState !== "online" && previousState !== "offline") {
    await env.STATE.put(STATE_KEY, currentState);
    console.log(`Initial state stored: ${currentState}`);
    return;
  }

  if (previousState === currentState) {
    return;
  }

  const message = currentState === "online"
    ? {
        title: "MF35X Tracker",
        body: "ESP32 ist wieder online – neue Tracker-Daten kommen wieder an."
      }
    : {
        title: "MF35X Tracker",
        body: "ESP32 ist offline – seit mindestens 45 Sekunden keine neuen Tracker-Daten."
      };

  // Zustand erst NACH erfolgreicher Push-Zustellung umschalten.
  // Bei einem temporaeren FCM-Fehler wird beim naechsten Cron-Lauf erneut versucht.
  await sendFcmNotification(env, message.title, message.body, currentState);
  await env.STATE.put(STATE_KEY, currentState);

  console.log(`State changed ${previousState} -> ${currentState}; push sent.`);
}

async function readCurrentStatus(env) {
  const firebaseRoot = String(env.FIREBASE_DB_URL || "").replace(/\/+$/, "");
  if (!firebaseRoot.startsWith("https://")) {
    throw new Error("FIREBASE_DB_URL fehlt oder ist ungueltig.");
  }

  const offlineAfterMs = parsePositiveInteger(
    env.OFFLINE_AFTER_MS,
    DEFAULT_OFFLINE_AFTER_MS
  );

  const response = await fetch(
    `${firebaseRoot}/tracker/live/timestamp.json`,
    {
      method: "GET",
      headers: { "accept": "application/json" },
      cf: { cacheTtl: 0, cacheEverything: false }
    }
  );

  if (!response.ok) {
    throw new Error(`Firebase HTTP ${response.status}`);
  }

  const raw = await response.json();
  const liveTimestamp = Number(raw);
  const now = Date.now();

  if (!Number.isFinite(liveTimestamp) || liveTimestamp <= 0) {
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

async function sendFcmNotification(env, title, body, state) {
  const fcmToken = String(env.FCM_TOKEN || "").trim();
  if (!fcmToken) {
    throw new Error("FCM_TOKEN fehlt.");
  }

  const serviceAccount = parseServiceAccount(env.GOOGLE_SERVICE_ACCOUNT_JSON);
  const accessToken = await getGoogleAccessToken(serviceAccount);

  const projectId =
    String(env.FIREBASE_PROJECT_ID || "").trim() ||
    String(serviceAccount.project_id || "").trim();

  if (!projectId) {
    throw new Error("Firebase project_id fehlt.");
  }

  const message = {
    message: {
      token: fcmToken,
      notification: {
        title,
        body
      },
      webpush: {
        headers: {
          Urgency: "high"
        },
        notification: {
          tag: "mf35x-esp32-status",
          renotify: true
        },
        data: {
          mf35xState: state
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
        "authorization": `Bearer ${accessToken}`,
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

function parseServiceAccount(raw) {
  if (!raw) {
    throw new Error("GOOGLE_SERVICE_ACCOUNT_JSON fehlt.");
  }

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

  const body = new URLSearchParams({
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
      body
    }
  );

  if (!response.ok) {
    const text = await response.text();
    throw new Error(`Google OAuth HTTP ${response.status}: ${text.slice(0, 500)}`);
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

function parsePositiveInteger(value, fallback) {
  const number = Number(value);
  return Number.isFinite(number) && number > 0
    ? Math.floor(number)
    : fallback;
}

function jsonResponse(value, status = 200) {
  return new Response(JSON.stringify(value, null, 2), {
    status,
    headers: {
      "content-type": "application/json; charset=utf-8",
      "cache-control": "no-store"
    }
  });
}
