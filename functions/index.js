const crypto = require("crypto");
const { setGlobalOptions } = require("firebase-functions/v2");
const { onCall, HttpsError } = require("firebase-functions/v2/https");
const { onSchedule } = require("firebase-functions/v2/scheduler");
const logger = require("firebase-functions/logger");
const { initializeApp } = require("firebase-admin/app");
const { getDatabase } = require("firebase-admin/database");
const { getMessaging } = require("firebase-admin/messaging");

initializeApp();
setGlobalOptions({ region: "europe-west1", maxInstances: 2 });

// Der ESP32 schreibt tracker/device/timestamp im aktuellen Firmwarestand
// regelmaessig als Firebase-Serverzeit. 35 s Reserve verhindert Fehlalarme
// bei kurzen LTE-/Firebase-Unterbrechungen.
const OFFLINE_AFTER_MS = 35_000;
const HEARTBEAT_PATH = "tracker/device/timestamp";
const SUBSCRIPTIONS_PATH = "tracker/push/subscriptions";
const STATE_PATH = "tracker/push/monitorState";

function tokenKey(token) {
  return crypto.createHash("sha256").update(token).digest("hex").slice(0, 48);
}

function validateToken(raw) {
  const token = String(raw || "").trim();
  if (token.length < 80 || token.length > 4096) {
    throw new HttpsError("invalid-argument", "Ungueltiges FCM-Registrierungstoken.");
  }
  return token;
}

exports.registerPushDevice = onCall(async request => {
  const token = validateToken(request.data?.token);
  const label = String(request.data?.label || "MF35X Web Push").slice(0, 120);
  const id = tokenKey(token);
  const now = Date.now();

  await getDatabase().ref(`${SUBSCRIPTIONS_PATH}/${id}`).set({
    token,
    label,
    createdAt: now,
    updatedAt: now
  });

  return { ok: true, id };
});

exports.unregisterPushDevice = onCall(async request => {
  const token = validateToken(request.data?.token);
  const id = tokenKey(token);
  await getDatabase().ref(`${SUBSCRIPTIONS_PATH}/${id}`).remove();
  return { ok: true };
});

exports.monitorEsp32Status = onSchedule(
  {
    schedule: "* * * * *",
    timeZone: "Europe/Vienna",
    retryCount: 0
  },
  async () => {
    const db = getDatabase();
    const now = Date.now();

    const [heartbeatSnap, stateSnap, subscriptionsSnap] = await Promise.all([
      db.ref(HEARTBEAT_PATH).once("value"),
      db.ref(STATE_PATH).once("value"),
      db.ref(SUBSCRIPTIONS_PATH).once("value")
    ]);

    const heartbeatAt = Number(heartbeatSnap.val() || 0);
    const heartbeatAgeMs = heartbeatAt > 0 ? Math.max(0, now - heartbeatAt) : null;
    const currentState =
      heartbeatAt > 0 && heartbeatAgeMs <= OFFLINE_AFTER_MS ? "online" : "offline";

    const oldState = stateSnap.val() || {};
    const previousState = oldState.current || null;

    // Beim ersten Lauf nur den Ausgangszustand merken. Dadurch entsteht nach
    // dem Deploy keine kuenstliche "online"- oder "offline"-Meldung.
    if (!previousState) {
      await db.ref(STATE_PATH).set({
        current: currentState,
        changedAt: now,
        lastCheckAt: now,
        lastHeartbeatAt: heartbeatAt || null,
        heartbeatAgeMs
      });
      logger.info("MF35X Push-Monitor initialisiert", { currentState, heartbeatAgeMs });
      return;
    }

    if (previousState === currentState) {
      await db.ref(STATE_PATH).update({
        lastCheckAt: now,
        lastHeartbeatAt: heartbeatAt || null,
        heartbeatAgeMs
      });
      return;
    }

    const title = "MF35X Tracker";
    const body =
      currentState === "online"
        ? "ESP32 ist wieder online."
        : "ESP32 ist offline – es kommen keine aktuellen Statusdaten mehr an.";

    const entries = Object.entries(subscriptionsSnap.val() || {});
    const tokens = entries
      .map(([, value]) => (typeof value === "string" ? value : value?.token))
      .filter(token => typeof token === "string" && token.length >= 80)
      .slice(0, 500);

    if (tokens.length) {
      const response = await getMessaging().sendEachForMulticast({
        tokens,
        data: {
          type: "esp32_status",
          state: currentState,
          title,
          body,
          changedAt: String(now),
          url: "./"
        }
      });

      const invalidCodes = new Set([
        "messaging/registration-token-not-registered",
        "messaging/invalid-registration-token",
        "messaging/invalid-argument"
      ]);
      const cleanup = [];

      response.responses.forEach((result, index) => {
        if (result.success) return;
        const code = result.error?.code || "";
        if (!invalidCodes.has(code)) return;
        const id = tokenKey(tokens[index]);
        cleanup.push(db.ref(`${SUBSCRIPTIONS_PATH}/${id}`).remove());
      });

      if (cleanup.length) await Promise.allSettled(cleanup);

      logger.info("MF35X Status-Push gesendet", {
        currentState,
        successCount: response.successCount,
        failureCount: response.failureCount
      });
    } else {
      logger.info("MF35X Statuswechsel ohne registrierte Push-Geraete", { currentState });
    }

    await db.ref(STATE_PATH).set({
      current: currentState,
      changedAt: now,
      lastCheckAt: now,
      lastHeartbeatAt: heartbeatAt || null,
      heartbeatAgeMs
    });
  }
);
