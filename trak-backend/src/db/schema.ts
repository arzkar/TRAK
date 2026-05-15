import {
  pgTable, text, uuid, timestamp, integer,
  real, boolean, bigserial, jsonb, index, unique,
} from 'drizzle-orm/pg-core'
import { sql } from 'drizzle-orm'

// ─── devices ──────────────────────────────────────────────────────────────────
export const devices = pgTable('devices', {
  id:        text('id').primaryKey(),
  name:      text('name').notNull(),
  apiKey:    text('api_key').notNull().unique(),
  vehicle:   text('vehicle'),
  createdAt: timestamp('created_at', { withTimezone: true }).defaultNow(),
  lastSeen:  timestamp('last_seen',  { withTimezone: true }),
})

// ─── sessions ─────────────────────────────────────────────────────────────────
export const sessions = pgTable('sessions', {
  id:            uuid('id').primaryKey().defaultRandom(),
  deviceId:      text('device_id').notNull().references(() => devices.id),
  startedAt:     timestamp('started_at',  { withTimezone: true }),
  endedAt:       timestamp('ended_at',    { withTimezone: true }),
  distanceKm:    real('distance_km'),
  maxSpeedKmh:   real('max_speed_kmh'),
  maxLeanDeg:    real('max_lean_deg'),
  maxRpm:        integer('max_rpm'),
  crashDetected: boolean('crash_detected').default(false),
  crashAt:       timestamp('crash_at', { withTimezone: true }),
  notes:         text('notes'),
  createdAt:     timestamp('created_at', { withTimezone: true }).defaultNow(),
}, (t) => ({
  deviceIdx: index('idx_sessions_device').on(t.deviceId, t.startedAt),
}))

// ─── telemetry ────────────────────────────────────────────────────────────────
// Parsed + computed values. TimescaleDB hypertable on `ts`.
export const telemetry = pgTable('telemetry', {
  id:         bigserial('id', { mode: 'number' }),
  deviceId:   text('device_id').notNull(),
  sessionId:  uuid('session_id').references(() => sessions.id, { onDelete: 'cascade' }),
  ts:         timestamp('ts', { withTimezone: true }).notNull(),
  batchIndex: integer('batch_index'),

  // OBD parsed
  rpm:        integer('rpm'),
  speedKmh:   real('speed_kmh'),
  throttle:   real('throttle'),
  coolant:    real('coolant'),
  vbat:       real('vbat'),

  // IMU raw
  ax: real('ax'), ay: real('ay'), az: real('az'),
  gx: real('gx'), gy: real('gy'), gz: real('gz'),

  // IMU computed
  lean:   real('lean'),
  pitch:  real('pitch'),
  gTotal: real('g_total'),
}, (t) => ({
  sessionIdx: index('idx_telemetry_session').on(t.sessionId, t.ts),
  deviceIdx:  index('idx_telemetry_device').on(t.deviceId,  t.ts),
}))

// ─── device_status ────────────────────────────────────────────────────────────
// Latest heartbeat per device (upserted).
export const deviceStatus = pgTable('device_status', {
  deviceId:        text('device_id').primaryKey().references(() => devices.id),
  lastSeen:        timestamp('last_seen',   { withTimezone: true }),
  sessionId:       uuid('session_id'),
  uptimeMs:        integer('uptime_ms'),
  wifiRssi:        integer('wifi_rssi'),
  freeHeapBytes:   integer('free_heap_bytes'),
  sdFreeMb:        real('sd_free_mb'),
  unsyncedBatches: integer('unsynced_batches'),
  obdConnected:    boolean('obd_connected'),
  imuOk:           boolean('imu_ok'),
  chipTemp:        real('chip_temp'),
  firmware:        text('firmware'),
})

// ─── events ───────────────────────────────────────────────────────────────────
export const events = pgTable('events', {
  id:        bigserial('id', { mode: 'number' }).primaryKey(),
  deviceId:  text('device_id').notNull(),
  sessionId: uuid('session_id').references(() => sessions.id),
  ts:        timestamp('ts', { withTimezone: true }).notNull(),
  type:      text('type').notNull(),  // 'crash' | 'session_start' | 'session_end'
  payload:   jsonb('payload'),
  createdAt: timestamp('created_at', { withTimezone: true }).defaultNow(),
}, (t) => ({
  sessionIdx: index('idx_events_session').on(t.sessionId, t.ts),
  typeIdx:    index('idx_events_type').on(t.deviceId, t.type, t.ts),
}))

// ─── TimescaleDB bootstrap SQL ────────────────────────────────────────────────
// Run manually once after `drizzle-kit push`:
//
//   SELECT create_hypertable('telemetry', 'ts', chunk_time_interval => INTERVAL '1 month');
//   CREATE INDEX idx_telemetry_lean ON telemetry(session_id, lean) WHERE lean IS NOT NULL;
//   CREATE INDEX idx_telemetry_rpm  ON telemetry(session_id, rpm)  WHERE rpm  IS NOT NULL;
//
//   ALTER TABLE telemetry SET (
//     timescaledb.compress,
//     timescaledb.compress_segmentby = 'session_id',
//     timescaledb.compress_orderby   = 'ts DESC'
//   );
//   SELECT add_compression_policy('telemetry', INTERVAL '7 days');
