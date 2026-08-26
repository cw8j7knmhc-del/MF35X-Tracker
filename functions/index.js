import { initializeApp } from "firebase-admin/app";
import { getDatabase } from "firebase-admin/database";
import { getMessaging } from "firebase-admin/messaging";
import { onValueCreated } from "firebase-functions/v2/database";
import * as logger from "firebase-functions/logger";

const DATABASE_URL =
  "https://tracker-989a9-default-rtdb.europe-west1.firebasedatabase.app";
const DATABASE_INSTANCE = "tracker-989a9-default-rtdb";
const FUNCTION_REGION = "europe-west1";
const MAX_FIDS_PER_SEND = 500;

initializeApp({ databaseURL: DATABASE_URL });

export const sendMf35xPush = onValueCreated(
  {
    ref: "/tracker/pushEvents/{eventId}",
    instance: DATABASE_INSTANCE,
    region: FUNCTION_REGION,
    maxInstances: 2
  },
  async event => {
    const alarm = event.data.val() || {};
    const eventId = event.params.eventId;

    const level = alarm.level === "alarm" ? "alarm" : "warning";
    const title = level === "alarm" ? "MF35X ALARM" : "MF35X Warnung";
    const body = String(alarm.text || "Neue Tracker-Meldung").slice(0, 220);
    const tag = `mf35x-${String(alarm.key || "alarm")}`.slice(0, 80);

    const db = getDatabase();
    const subscriptions = await db.ref("tracker/push/subscriptions").get();
    const raw = subscriptions.val() || {};

    const fids = Object.values(raw)
      .filter(item => item && item.enabled !== false && typeof item.fid === "string")
      .map(item => item.fid)
      .filter((fid, index, list) => fid.length > 0 && list.indexOf(fid) === index);

    if (!fids.length) {
      logger.info("MF35X Push: keine registrierten FIDs", { eventId, level, body });
      await db.ref("tracker/push/status").set({
        lastEventId: eventId,
        lastLevel: level,
        lastText: body,
        recipients: 0,
        success: 0,
        failed: 0,
        updatedAt: Date.now()
      });
      return;
    }

    let success = 0;
    let failed = 0;

    for (let start = 0; start < fids.length; start += MAX_FIDS_PER_SEND) {
      const batch = fids.slice(start, start + MAX_FIDS_PER_SEND);
      const response = await getMessaging().sendEachForMulticast({
        fids: batch,
        data: {
          title,
          body,
          level,
          tag,
          eventId,
          metric: String(alarm.metric || "")
        },
        webpush: {
          headers: {
            Urgency: level === "alarm" ? "high" : "normal",
            TTL: "300"
          }
        }
      });

      success += response.successCount;
      failed += response.failureCount;

      response.responses.forEach((result, index) => {
        if (!result.success) {
          logger.warn("MF35X Push-Ziel fehlgeschlagen", {
            eventId,
            fidSuffix: batch[index].slice(-8),
            error: result.error?.code || result.error?.message || "unknown"
          });
        }
      });
    }

    await db.ref("tracker/push/status").set({
      lastEventId: eventId,
      lastLevel: level,
      lastText: body,
      recipients: fids.length,
      success,
      failed,
      updatedAt: Date.now()
    });

    logger.info("MF35X Push versendet", {
      eventId,
      level,
      recipients: fids.length,
      success,
      failed
    });
  }
);
