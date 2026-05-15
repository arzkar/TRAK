# schema.spec.md
# TRAK — Database Schema Specification
# Updated: Option B — raw_telemetry table added, parsing in Hono

---

## Database

| Property | Value |
|---|---|
| Engine | PostgreSQL 16+ |
| Extension | TimescaleDB (hypertable for telemetry table) |
| ORM | Drizzle ORM |
| Connection | Pooled via postgres.js |
| Dev | Docker (timescale/timescaledb-ha:pg16) |
| Production | Neon / Supabase / self-hosted |

---

## Design — Two Layer Storage

```
MQTT payload arrives
        │
        ▼
raw_telemetry       ← entire JSON stored as-is, immediately, no parsing
        │
        ▼
  Hono parser       ← OBD hex decoded, IMU values computed
        │
        ▼
  telemetry         ← clean parsed values for analytics queries
```

Raw is truth. Telemetry is derived. If parsing logic changes, reprocess raw → telemetry.

---

## Tables

### devices

```sql
CREATE TABLE devices (
  id          TEXT PRIMARY KEY,
  name        TEXT NOT NULL,
  api_key     TEXT NOT NULL UNIQUE,
  vehicle     TEXT,
  created_at  TIMESTAMPTZ DEFAULT now(),
  last_seen   TIMESTAMPTZ
);
```

### sessions

```sql
CREATE TABLE sessions (
  id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  device_id       TEXT NOT NULL REFERENCES devices(id),
  started_at      TIMESTAMPTZ,
  ended_at        TIMESTAMPTZ,
  duration_s      INT GENERATED ALWAYS AS (
                    EXTRACT(EPOCH FROM (ended_at - started_at))::INT
                  ) STORED,
  distance_km     FLOAT,
  max_speed_kmh   FLOAT,
  max_lean_deg    FLOAT,
  max_rpm         INT,
  crash_detected  BOOLEAN DEFAULT FALSE,
  crash_at        TIMESTAMPTZ,
  notes           TEXT,
  created_at      TIMESTAMPTZ DEFAULT now()
);

CREATE INDEX idx_sessions_device ON sessions(device_id, started_at DESC);
```

### raw_telemetry

Stores entire MQTT payload as-is. Written first, before any parsing. Never modified.

```sql
CREATE TABLE raw_telemetry (
  id          BIGSERIAL PRIMARY KEY,
  device_id   TEXT NOT NULL,
  session_id  UUID REFERENCES sessions(id),
  batch_index INT NOT NULL,
  received_at TIMESTAMPTZ DEFAULT now(),
  payload     JSONB NOT NULL,
  processed   BOOLEAN DEFAULT FALSE,
  UNIQUE(device_id, session_id, batch_index)
);

CREATE INDEX idx_raw_telemetry_session   ON raw_telemetry(session_id);
CREATE INDEX idx_raw_telemetry_processed ON raw_telemetry(processed) WHERE processed = FALSE;
```

### telemetry

Parsed and computed values. Populated by Hono parser from raw_telemetry. TimescaleDB hypertable.

```sql
CREATE TABLE telemetry (
  id          BIGSERIAL,
  device_id   TEXT NOT NULL,
  session_id  UUID REFERENCES sessions(id) ON DELETE CASCADE,
  ts          TIMESTAMPTZ NOT NULL,
  batch_index INT,

  -- OBD parsed values
  rpm         INT,
  speed_kmh   FLOAT,
  throttle    FLOAT,
  coolant     FLOAT,
  iat         FLOAT,
  vbat        FLOAT,

  -- IMU raw values
  ax          FLOAT,
  ay          FLOAT,
  az          FLOAT,
  gx          FLOAT,
  gy          FLOAT,
  gz          FLOAT,

  -- IMU computed values
  lean        FLOAT,
  pitch       FLOAT,
  g_total     FLOAT
);

SELECT create_hypertable('telemetry', 'ts', chunk_time_interval => INTERVAL '1 month');

CREATE INDEX idx_telemetry_session ON telemetry(session_id, ts DESC);
CREATE INDEX idx_telemetry_device  ON telemetry(device_id, ts DESC);
CREATE INDEX idx_telemetry_lean    ON telemetry(session_id, lean) WHERE lean IS NOT NULL;
CREATE INDEX idx_telemetry_rpm     ON telemetry(session_id, rpm)  WHERE rpm IS NOT NULL;
```

### device_status

Latest heartbeat from each device.

```sql
CREATE TABLE device_status (
  device_id           TEXT PRIMARY KEY REFERENCES devices(id),
  last_seen           TIMESTAMPTZ,
  session_id          UUID,
  uptime_ms           BIGINT,
  wifi_rssi           INT,
  free_heap_bytes     INT,
  sd_free_mb          FLOAT,
  unsynced_batches    INT,
  obd_connected       BOOLEAN,
  imu_ok              BOOLEAN,
  chip_temp           FLOAT,
  firmware            TEXT
);
```

### events

Discrete events — crashes, calibrations, session boundaries.

```sql
CREATE TABLE events (
  id          BIGSERIAL PRIMARY KEY,
  device_id   TEXT NOT NULL,
  session_id  UUID REFERENCES sessions(id),
  ts          TIMESTAMPTZ NOT NULL,
  type        TEXT NOT NULL,
  payload     JSONB,
  created_at  TIMESTAMPTZ DEFAULT now()
);

CREATE INDEX idx_events_session ON events(session_id, ts);
CREATE INDEX idx_events_type    ON events(device_id, type, ts DESC);
```

---

## Drizzle Schema (TypeScript)

```typescript
import {
  pgTable, text, uuid, timestamp, integer,
  real, boolean, bigserial, jsonb, unique, index
} from 'drizzle-orm/pg-core'

export const devices = pgTable('devices', {
  id:        text('id').primaryKey(),
  name:      text('name').notNull(),
  apiKey:    text('api_key').notNull().unique(),
  vehicle:   text('vehicle'),
  createdAt: timestamp('created_at').defaultNow(),
  lastSeen:  timestamp('last_seen'),
})

export const sessions = pgTable('sessions', {
  id:            uuid('id').primaryKey().defaultRandom(),
  deviceId:      text('device_id').notNull().references(() => devices.id),
  startedAt:     timestamp('started_at'),
  endedAt:       timestamp('ended_at'),
  distanceKm:    real('distance_km'),
  maxSpeedKmh:   real('max_speed_kmh'),
  maxLeanDeg:    real('max_lean_deg'),
  maxRpm:        integer('max_rpm'),
  crashDetected: boolean('crash_detected').default(false),
  crashAt:       timestamp('crash_at'),
  notes:         text('notes'),
  createdAt:     timestamp('created_at').defaultNow(),
})

export const rawTelemetry = pgTable('raw_telemetry', {
  id:         bigserial('id', { mode: 'number' }).primaryKey(),
  deviceId:   text('device_id').notNull(),
  sessionId:  uuid('session_id').references(() => sessions.id),
  batchIndex: integer('batch_index').notNull(),
  receivedAt: timestamp('received_at').defaultNow(),
  payload:    jsonb('payload').notNull(),
  processed:  boolean('processed').default(false),
}, (t) => ({
  unq: unique().on(t.deviceId, t.sessionId, t.batchIndex),
}))

export const telemetry = pgTable('telemetry', {
  id:         bigserial('id', { mode: 'number' }),
  deviceId:   text('device_id').notNull(),
  sessionId:  uuid('session_id').references(() => sessions.id, { onDelete: 'cascade' }),
  ts:         timestamp('ts').notNull(),
  batchIndex: integer('batch_index'),
  rpm:        integer('rpm'),
  speedKmh:   real('speed_kmh'),
  throttle:   real('throttle'),
  coolant:    real('coolant'),
  iat:        real('iat'),
  vbat:       real('vbat'),
  ax:         real('ax'),
  ay:         real('ay'),
  az:         real('az'),
  gx:         real('gx'),
  gy:         real('gy'),
  gz:         real('gz'),
  lean:       real('lean'),
  pitch:      real('pitch'),
  gTotal:     real('g_total'),
})

export const deviceStatus = pgTable('device_status', {
  deviceId:        text('device_id').primaryKey().references(() => devices.id),
  lastSeen:        timestamp('last_seen'),
  sessionId:       uuid('session_id'),
  uptimeMs:        bigserial('uptime_ms', { mode: 'number' }),
  wifiRssi:        integer('wifi_rssi'),
  freeHeapBytes:   integer('free_heap_bytes'),
  sdFreeMb:        real('sd_free_mb'),
  unsyncedBatches: integer('unsynced_batches'),
  obdConnected:    boolean('obd_connected'),
  imuOk:           boolean('imu_ok'),
  chipTemp:        real('chip_temp'),
  firmware:        text('firmware'),
})

export const events = pgTable('events', {
  id:        bigserial('id', { mode: 'number' }).primaryKey(),
  deviceId:  text('device_id').notNull(),
  sessionId: uuid('session_id').references(() => sessions.id),
  ts:        timestamp('ts').notNull(),
  type:      text('type').notNull(),
  payload:   jsonb('payload'),
  createdAt: timestamp('created_at').defaultNow(),
})
```

---

## Hono Parser (obd-parser.ts)

```typescript
function parseHex(hex: string | null, byteCount: number): number[] | null {
  if (!hex || ['NO DATA', '?', 'BUS BUSY'].includes(hex.trim())) return null
  const clean = hex.replace(/\s/g, '').slice(4) // strip response header
  const bytes = []
  for (let i = 0; i < byteCount * 2; i += 2) {
    bytes.push(parseInt(clean.slice(i, i + 2), 16))
  }
  return bytes.some(isNaN) ? null : bytes
}

export function parseRPM(hex: string | null): number | null {
  const b = parseHex(hex, 2)
  return b ? ((b[0] * 256) + b[1]) / 4 : null
}

export function parseSpeed(hex: string | null): number | null {
  const b = parseHex(hex, 1)
  return b ? b[0] : null
}

export function parseThrottle(hex: string | null): number | null {
  const b = parseHex(hex, 1)
  return b ? b[0] * 100 / 255 : null
}

export function parseCoolant(hex: string | null): number | null {
  const b = parseHex(hex, 1)
  return b ? b[0] - 40 : null
}

export function parseVoltage(hex: string | null): number | null {
  const b = parseHex(hex, 2)
  return b ? ((b[0] * 256) + b[1]) / 1000 : null
}
```

## Hono Parser (imu-parser.ts)

```typescript
export function computeLean(ay: number, az: number): number {
  return Math.atan2(ay, az) * (180 / Math.PI)
}

export function computePitch(ax: number, az: number): number {
  return Math.atan2(-ax, az) * (180 / Math.PI)
}

export function computeGTotal(ax: number, ay: number, az: number): number {
  return Math.sqrt(ax * ax + ay * ay + az * az)
}
```

---

## Batch Ingestion Logic

```typescript
async function handleTelemetryBatch(deviceId: string, batch: RawBatch) {
  // 1. Store raw immediately — no parsing, just insert
  await db.insert(rawTelemetry).values({
    deviceId,
    sessionId:  batch.session_id,
    batchIndex: batch.batch_index,
    payload:    batch,
    processed:  false,
  }).onConflictDoNothing() // idempotent — duplicate batches ignored

  // 2. Upsert session
  await db.insert(sessions)
    .values({ id: batch.session_id, deviceId, startedAt: new Date(batch.batch_start) })
    .onConflictDoNothing()

  // 3. Parse and insert into telemetry
  const rows = batch.readings.map(r => ({
    deviceId,
    sessionId:  batch.session_id,
    ts:         new Date(batch.batch_start + r.t),
    batchIndex: batch.batch_index,
    rpm:        parseRPM(r.obd['010C']),
    speedKmh:   parseSpeed(r.obd['010D']),
    throttle:   parseThrottle(r.obd['0111']),
    coolant:    parseCoolant(r.obd['0105']),
    vbat:       parseVoltage(r.obd['0142']),
    ax: r.imu.ax, ay: r.imu.ay, az: r.imu.az,
    gx: r.imu.gx, gy: r.imu.gy, gz: r.imu.gz,
    lean:   computeLean(r.imu.ay, r.imu.az),
    pitch:  computePitch(r.imu.ax, r.imu.az),
    gTotal: computeGTotal(r.imu.ax, r.imu.ay, r.imu.az),
  }))

  await db.insert(telemetry).values(rows)

  // 4. Update session stats
  const maxRpm   = Math.max(...rows.map(r => r.rpm   ?? 0))
  const maxSpeed = Math.max(...rows.map(r => r.speedKmh ?? 0))
  const maxLean  = Math.max(...rows.map(r => Math.abs(r.lean ?? 0)))

  await db.update(sessions)
    .set({
      maxRpm:       sql`GREATEST(max_rpm, ${maxRpm})`,
      maxSpeedKmh:  sql`GREATEST(max_speed_kmh, ${maxSpeed})`,
      maxLeanDeg:   sql`GREATEST(max_lean_deg, ${maxLean})`,
    })
    .where(eq(sessions.id, batch.session_id))

  // 5. Mark raw batch as processed
  await db.update(rawTelemetry)
    .set({ processed: true })
    .where(and(
      eq(rawTelemetry.deviceId, deviceId),
      eq(rawTelemetry.sessionId, batch.session_id),
      eq(rawTelemetry.batchIndex, batch.batch_index)
    ))
}
```

---

## Reprocessing (when parser logic changes)

```typescript
// reprocess.ts — reparse all raw batches into telemetry table
async function reprocessAll() {
  // Clear existing parsed data
  await db.delete(telemetry)

  // Replay all raw batches in order
  const batches = await db.select()
    .from(rawTelemetry)
    .orderBy(rawTelemetry.receivedAt)

  for (const batch of batches) {
    await handleTelemetryBatch(batch.deviceId, batch.payload)
    console.log(`Reprocessed batch ${batch.batchIndex} for ${batch.deviceId}`)
  }
}
```

Run this anytime you fix a parser bug — all historical data gets corrected automatically.

---

## TimescaleDB Configuration

```sql
-- Compression after 7 days
ALTER TABLE telemetry SET (
  timescaledb.compress,
  timescaledb.compress_segmentby = 'session_id',
  timescaledb.compress_orderby = 'ts DESC'
);

SELECT add_compression_policy('telemetry', INTERVAL '7 days');

-- 1-minute continuous aggregate for fast dashboard queries
CREATE MATERIALIZED VIEW telemetry_1min
WITH (timescaledb.continuous) AS
SELECT
  time_bucket('1 minute', ts) AS bucket,
  session_id,
  AVG(rpm)       AS avg_rpm,
  MAX(rpm)       AS max_rpm,
  AVG(speed_kmh) AS avg_speed,
  MAX(ABS(lean)) AS max_lean,
  AVG(g_total)   AS avg_g
FROM telemetry
GROUP BY bucket, session_id
WITH NO DATA;

SELECT add_continuous_aggregate_policy('telemetry_1min',
  start_offset => INTERVAL '1 hour',
  end_offset   => INTERVAL '1 minute',
  schedule_interval => INTERVAL '1 minute'
);
```
