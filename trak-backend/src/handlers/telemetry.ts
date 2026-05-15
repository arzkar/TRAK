import { db } from '../db/client'
import {
  sessions, telemetry, devices,
} from '../db/schema'
import { eq, sql } from 'drizzle-orm'
import { parseRPM, parseSpeed, parseThrottle, parseCoolant, parseVoltage } from '../parsers/obd-parser'
import { computeLean, computePitch, computeGTotal } from '../parsers/imu-parser'
import * as fs from 'node:fs/promises'
import * as path from 'node:path'

// ─── Types matching the ESP32 MQTT payload (Option B) ─────────────────────────
interface OBDBlock {
  '010C'?: string | null
  '010D'?: string | null
  '0111'?: string | null
  '0105'?: string | null
  '0142'?: string | null
}

interface IMUBlock {
  ax: number; ay: number; az: number
  gx: number; gy: number; gz: number
}

interface Reading {
  t: number
  obd: OBDBlock
  imu: IMUBlock
}

interface DeviceBlock {
  chip_temp: number
  free_heap:  number
  uptime_ms:  number
  wifi_rssi:  number
  cpu_freq_mhz: number
}

export interface TelemetryBatch {
  v:                 number
  device_id:         string
  session_id:        string
  firmware:          string
  batch_index:       number
  batch_start:       number   // epoch ms
  batch_duration_ms: number
  reading_count:     number
  device:            DeviceBlock
  readings:          Reading[]
}

// ─── File Storage Helper ──────────────────────────────────────────────────────
async function saveRawToDisk(batch: TelemetryBatch) {
  const baseDir = path.join(process.cwd(), 'storage', 'raw_telemetry', batch.device_id)
  await fs.mkdir(baseDir, { recursive: true })
  
  const filename = `${batch.session_id}_${batch.batch_index.toString().padStart(5, '0')}.json`
  const filePath = path.join(baseDir, filename)
  
  await fs.writeFile(filePath, JSON.stringify(batch, null, 2))
}

// ─── Main handler ─────────────────────────────────────────────────────────────
export async function handleTelemetryBatch(batch: TelemetryBatch): Promise<void> {
  const { device_id, session_id, batch_index } = batch

  // 1. Ensure device exists (auto-register in dev — lock down in prod)
  await db.insert(devices)
    .values({ id: device_id, name: device_id, apiKey: `trak_dev_${device_id}` })
    .onConflictDoNothing()

  // 2. Store raw payload to disk instead of DB
  try {
    await saveRawToDisk(batch)
  } catch (err) {
    console.error(`[telemetry] Failed to save raw file for batch ${batch_index}:`, err)
  }

  // 3. Upsert session record
  await db.insert(sessions)
    .values({
      id:        session_id,
      deviceId:  device_id,
      startedAt: new Date(batch.batch_start),
    })
    .onConflictDoNothing()

  // 4. Parse each reading and insert into telemetry table
  if (!batch.readings?.length) {
    console.warn(`[telemetry] Batch ${batch_index} from ${device_id} has no readings`)
    return
  }

  const rows = batch.readings.map((r) => ({
    deviceId:   device_id,
    sessionId:  session_id,
    ts:         new Date(batch.batch_start + r.t),
    batchIndex: batch_index,

    // OBD parsed values — null if ELM327 returned an error string
    rpm:      parseRPM(r.obd?.['010C']),
    speedKmh: parseSpeed(r.obd?.['010D']),
    throttle: parseThrottle(r.obd?.['0111']),
    coolant:  parseCoolant(r.obd?.['0105']),
    vbat:     parseVoltage(r.obd?.['0142']),

    // IMU raw
    ax: r.imu?.ax ?? null,
    ay: r.imu?.ay ?? null,
    az: r.imu?.az ?? null,
    gx: r.imu?.gx ?? null,
    gy: r.imu?.gy ?? null,
    gz: r.imu?.gz ?? null,

    // IMU computed — null if IMU data absent
    lean:   (r.imu?.ay != null && r.imu?.az != null)
              ? computeLean(r.imu.ay, r.imu.az)   : null,
    pitch:  (r.imu?.ax != null && r.imu?.az != null)
              ? computePitch(r.imu.ax, r.imu.az)  : null,
    gTotal: (r.imu?.ax != null && r.imu?.ay != null && r.imu?.az != null)
              ? computeGTotal(r.imu.ax, r.imu.ay, r.imu.az) : null,
  }))

  await db.insert(telemetry).values(rows)

  // 5. Update session aggregate stats
  const maxRpm   = Math.max(...rows.map(r => r.rpm   ?? 0))
  const maxSpeed = Math.max(...rows.map(r => r.speedKmh ?? 0))
  const maxLean  = Math.max(...rows.map(r => Math.abs(r.lean ?? 0)))

  await db.update(sessions)
    .set({
      maxRpm:      sql`GREATEST(COALESCE(max_rpm, 0),       ${maxRpm})`,
      maxSpeedKmh: sql`GREATEST(COALESCE(max_speed_kmh, 0), ${maxSpeed})`,
      maxLeanDeg:  sql`GREATEST(COALESCE(max_lean_deg, 0),  ${maxLean})`,
    })
    .where(eq(sessions.id, session_id))

  // 6. Update device last_seen
  await db.update(devices)
    .set({ lastSeen: new Date() })
    .where(eq(devices.id, device_id))

  console.log(`[telemetry] ✓ Batch ${batch_index} (${rows.length} readings) saved to disk + DB — ${device_id}`)
}
