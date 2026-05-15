-- 1. Enable TimescaleDB Extension (usually pre-enabled in the image)
CREATE EXTENSION IF NOT EXISTS timescaledb CASCADE;

-- 2. Create Hypertables (must be done after drizzle-kit push)
-- This script assumes the tables already exist.
SELECT create_hypertable('telemetry', 'ts', if_not_exists => TRUE, chunk_time_interval => INTERVAL '1 month');

-- 3. Optimization Indices
CREATE INDEX IF NOT EXISTS idx_telemetry_lean ON telemetry(session_id, lean) WHERE lean IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_telemetry_rpm  ON telemetry(session_id, rpm)  WHERE rpm  IS NOT NULL;

-- 4. Compression
ALTER TABLE telemetry SET (
  timescaledb.compress,
  timescaledb.compress_segmentby = 'session_id',
  timescaledb.compress_orderby   = 'ts DESC'
);

-- Add compression policy (ignore if already exists)
SELECT add_compression_policy('telemetry', INTERVAL '7 days', if_not_exists => TRUE);
