# analytics.spec.md
# TRAK — Analytics Specification

---

## Overview

All analytics queries run against the `telemetry` table (TimescaleDB hypertable). For dashboard use, the `telemetry_1min` continuous aggregate handles sub-second response on large datasets. Raw queries are used for detailed per-session drill-downs.

---

## RPM Band Distribution

Classifies engine time into operating bands. Reveals riding style and engine usage patterns.

```sql
SELECT
  CASE
    WHEN rpm < 1200 THEN 'idle'
    WHEN rpm < 3000 THEN 'cruise'
    WHEN rpm < 5500 THEN 'pull'
    ELSE 'redline'
  END AS band,
  COUNT(*) * 0.1 AS seconds,       -- assumes 10Hz sampling
  ROUND(COUNT(*) * 100.0 / SUM(COUNT(*)) OVER (), 1) AS percent
FROM telemetry
WHERE session_id = $1
  AND rpm IS NOT NULL
GROUP BY band
ORDER BY MIN(rpm);
```

CB350 RPM band definitions:
| Band | Range | Meaning |
|---|---|---|
| idle | < 1200 RPM | Stopped, neutral, warming up |
| cruise | 1200–3000 RPM | Normal city / highway |
| pull | 3000–5500 RPM | Active acceleration |
| redline | > 5500 RPM | Hard acceleration, near limiter |

---

## Lean Angle Analysis

### Max lean per session
```sql
SELECT
  session_id,
  MAX(lean) AS max_right_lean,
  MIN(lean) AS max_left_lean,
  MAX(ABS(lean)) AS max_lean_absolute
FROM telemetry
WHERE session_id = $1
GROUP BY session_id;
```

### Lean histogram (time in each band)
```sql
SELECT
  CASE
    WHEN lean BETWEEN  0 AND  5 THEN '0-5° right'
    WHEN lean BETWEEN  5 AND 15 THEN '5-15° right'
    WHEN lean BETWEEN 15 AND 30 THEN '15-30° right'
    WHEN lean > 30 THEN '30°+ right'
    WHEN lean BETWEEN -5 AND  0 THEN '0-5° left'
    WHEN lean BETWEEN -15 AND -5 THEN '5-15° left'
    WHEN lean BETWEEN -30 AND -15 THEN '15-30° left'
    WHEN lean < -30 THEN '30°+ left'
  END AS band,
  COUNT(*) * 0.1 AS seconds
FROM telemetry
WHERE session_id = $1
  AND lean IS NOT NULL
GROUP BY band;
```

---

## G-Force Analysis

### G-force distribution
```sql
SELECT
  CASE
    WHEN g_total < 1.1 THEN 'smooth (< 0.1g net)'
    WHEN g_total < 1.3 THEN 'light load'
    WHEN g_total < 1.5 THEN 'moderate load'
    WHEN g_total < 2.0 THEN 'heavy load'
    ELSE 'extreme (> 1g net)'
  END AS band,
  COUNT(*) * 0.1 AS seconds
FROM telemetry
WHERE session_id = $1
GROUP BY band;
```

### Hard braking events
```sql
SELECT
  ts,
  speed_kmh,
  ax AS longitudinal_g,
  g_total
FROM telemetry
WHERE session_id = $1
  AND ax < -0.4              -- braking threshold: -0.4g
  AND speed_kmh > 10         -- exclude low-speed events (parking)
ORDER BY ax ASC;             -- most severe first
```

### Hard acceleration events
```sql
SELECT
  ts,
  speed_kmh,
  rpm,
  ax AS longitudinal_g
FROM telemetry
WHERE session_id = $1
  AND ax > 0.3               -- acceleration threshold: +0.3g
ORDER BY ax DESC;
```

---

## Speed Analysis

### Speed percentile distribution
```sql
SELECT
  PERCENTILE_CONT(0.50) WITHIN GROUP (ORDER BY speed_kmh) AS p50,
  PERCENTILE_CONT(0.75) WITHIN GROUP (ORDER BY speed_kmh) AS p75,
  PERCENTILE_CONT(0.90) WITHIN GROUP (ORDER BY speed_kmh) AS p90,
  PERCENTILE_CONT(0.95) WITHIN GROUP (ORDER BY speed_kmh) AS p95,
  MAX(speed_kmh) AS max_speed
FROM telemetry
WHERE session_id = $1
  AND speed_kmh IS NOT NULL;
```

### Time at speed bands
```sql
SELECT
  CASE
    WHEN speed_kmh < 20  THEN 'slow (< 20)'
    WHEN speed_kmh < 60  THEN 'city (20–60)'
    WHEN speed_kmh < 100 THEN 'highway (60–100)'
    ELSE 'fast (> 100)'
  END AS band,
  COUNT(*) * 0.1 AS seconds
FROM telemetry
WHERE session_id = $1
  AND speed_kmh IS NOT NULL
GROUP BY band;
```

---

## Engine Health

### Coolant temperature over session
```sql
SELECT
  time_bucket('1 minute', ts) AS minute,
  AVG(coolant) AS avg_coolant,
  MAX(coolant) AS max_coolant
FROM telemetry
WHERE session_id = $1
  AND coolant IS NOT NULL
GROUP BY minute
ORDER BY minute;
```

### Battery voltage trend
```sql
SELECT
  time_bucket('5 minutes', ts) AS bucket,
  AVG(vbat) AS avg_voltage,
  MIN(vbat) AS min_voltage
FROM telemetry
WHERE session_id = $1
  AND vbat IS NOT NULL
GROUP BY bucket
ORDER BY bucket;
```

### Throttle vs RPM scatter (engine load curve)
```sql
SELECT
  ROUND(throttle / 5) * 5 AS throttle_bucket,   -- group by 5% increments
  ROUND(rpm / 500) * 500 AS rpm_bucket,          -- group by 500 RPM
  COUNT(*) AS sample_count
FROM telemetry
WHERE session_id = $1
  AND throttle IS NOT NULL
  AND rpm IS NOT NULL
GROUP BY throttle_bucket, rpm_bucket
ORDER BY throttle_bucket, rpm_bucket;
```

---

## Session Comparison

Compare key metrics across multiple sessions. Useful for tracking improvement over time.

```sql
SELECT
  s.id AS session_id,
  s.started_at,
  s.duration_s,
  s.distance_km,
  s.max_speed_kmh,
  s.max_lean_deg,
  s.max_rpm,
  COUNT(CASE WHEN t.ax < -0.4 THEN 1 END) AS hard_braking_count,
  COUNT(CASE WHEN t.ax >  0.3 THEN 1 END) AS hard_accel_count,
  AVG(t.rpm) AS avg_rpm,
  AVG(ABS(t.lean)) AS avg_lean,
  s.crash_detected
FROM sessions s
JOIN telemetry t ON t.session_id = s.id
WHERE s.id = ANY($1::uuid[])         -- array of session UUIDs
GROUP BY s.id, s.started_at, s.duration_s, s.distance_km,
         s.max_speed_kmh, s.max_lean_deg, s.max_rpm, s.crash_detected
ORDER BY s.started_at DESC;
```

---

## Crash Analysis

### Crash event detail with pre-crash context
```sql
-- Get 10 seconds of data before crash timestamp
SELECT
  ts,
  speed_kmh,
  rpm,
  lean,
  ax,
  ay,
  az,
  g_total
FROM telemetry
WHERE session_id = $1
  AND ts BETWEEN ($2::timestamptz - INTERVAL '10 seconds') AND $2::timestamptz
ORDER BY ts;
```

---

## Riding Style Score

Computed metric — 0 to 100. Higher = more aggressive.

```sql
WITH stats AS (
  SELECT
    COUNT(CASE WHEN ax < -0.4 THEN 1 END) AS hard_braking,
    COUNT(CASE WHEN ax >  0.3 THEN 1 END) AS hard_accel,
    COUNT(CASE WHEN ABS(lean) > 30 THEN 1 END) AS high_lean,
    COUNT(CASE WHEN rpm > 5500 THEN 1 END) AS redline_samples,
    COUNT(*) AS total
  FROM telemetry
  WHERE session_id = $1
)
SELECT
  LEAST(100, ROUND(
    (hard_braking   * 15.0 / NULLIF(total, 0) * 1000) +
    (hard_accel     * 10.0 / NULLIF(total, 0) * 1000) +
    (high_lean      * 20.0 / NULLIF(total, 0) * 1000) +
    (redline_samples * 5.0 / NULLIF(total, 0) * 1000)
  )) AS style_score
FROM stats;
```

Score interpretation:
| Score | Label |
|---|---|
| 0–20 | Smooth / economy |
| 21–40 | Relaxed |
| 41–60 | Spirited |
| 61–80 | Aggressive |
| 81–100 | Track-style |

---

## Vibration Signature (Engine Health)

FFT of vertical G-force (az) correlated with RPM. Abnormal frequency spikes at specific RPM ranges indicate potential mechanical issues. Implemented in post-processing, not real-time.

```sql
-- Get raw az samples at specific RPM range for FFT input
SELECT ts, az
FROM telemetry
WHERE session_id = $1
  AND rpm BETWEEN 3000 AND 3200   -- specific RPM window
  AND az IS NOT NULL
ORDER BY ts;
-- Feed this into a Python FFT pipeline for frequency analysis
```

---

## Continuous Aggregate Queries (Fast Dashboard)

Use `telemetry_1min` for dashboard timeline charts — sub-millisecond response vs seconds on raw table:

```sql
-- Session timeline at 1-minute resolution
SELECT
  bucket,
  avg_rpm,
  max_rpm,
  avg_speed,
  max_lean,
  avg_g
FROM telemetry_1min
WHERE session_id = $1
ORDER BY bucket;
```
