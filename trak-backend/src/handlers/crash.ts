import { db } from '../db/client'
import { sessions, events, deviceStatus, devices } from '../db/schema'
import { eq } from 'drizzle-orm'

export interface CrashPayload {
  v:                  number
  device_id:          string
  session_id:         string
  crash_timestamp:    number
  raw_imu_at_crash:   { ax: number; ay: number; az: number; gx: number; gy: number; gz: number }
  g_total_at_crash:   number
  obd_speed_raw?:     string
  pre_crash_batch_index?: number
}

export async function handleCrashEvent(payload: CrashPayload): Promise<void> {
  const { device_id, session_id, crash_timestamp, g_total_at_crash } = payload
  const crashAt = new Date(crash_timestamp)

  console.warn(`[CRASH] !! device=${device_id} g_total=${g_total_at_crash} at ${crashAt.toISOString()}`)

  // Update session crash flag
  await db.update(sessions)
    .set({ crashDetected: true, crashAt })
    .where(eq(sessions.id, session_id))

  // Insert discrete crash event for timeline queries
  await db.insert(events).values({
    deviceId:  device_id,
    sessionId: session_id,
    ts:        crashAt,
    type:      'crash',
    payload:   payload as any,
  })
}
