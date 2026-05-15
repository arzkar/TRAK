# api.spec.md
# TRAK — API Specification

---

## Overview

| Property | Value |
|---|---|
| Framework | Hono |
| Runtime | Bun |
| Base URL (dev) | `http://localhost:3000` |
| Base URL (prod) | `https://api.trak.dev` |
| Auth | API key via `X-API-Key` header |
| Content-Type | `application/json` |
| Validation | Zod schemas on all request bodies |

---

## Authentication

All endpoints except `/health` require `X-API-Key` header.

```
X-API-Key: trak_sk_xxxxxxxxxxxxxxxxxxxx
```

API keys are per-device, stored hashed in the `devices` table. The ESP32 includes its key in every MQTT message via the `device_id` field — the backend validates on ingestion.

HTTP endpoints are for the dashboard/analytics frontend and use the same key scheme.

---

## Error Response Format

```json
{
  "error": "session_not_found",
  "message": "Session 550e8400 does not exist",
  "status": 404
}
```

| Status | Meaning |
|---|---|
| 200 | Success |
| 201 | Created |
| 400 | Bad request / validation error |
| 401 | Missing or invalid API key |
| 404 | Resource not found |
| 409 | Conflict (duplicate batch) |
| 422 | Unprocessable entity |
| 500 | Internal server error |

---

## Endpoints

---

### GET /health

Health check. No auth required.

**Response 200:**
```json
{
  "status": "ok",
  "timestamp": 1717234222000,
  "db": "ok",
  "mqtt": "connected"
}
```

---

### GET /sessions

List all sessions for a device.

**Query params:**
| Param | Type | Default | Description |
|---|---|---|---|
| device_id | string | required | Filter by device |
| limit | int | 20 | Max results |
| offset | int | 0 | Pagination offset |
| from | ISO8601 | — | Filter sessions after this time |
| to | ISO8601 | — | Filter sessions before this time |

**Response 200:**
```json
{
  "sessions": [
    {
      "id": "550e8400-e29b-41d4-a716-446655440000",
      "device_id": "trak-cb350-001",
      "started_at": "2025-06-01T14:30:22Z",
      "ended_at": "2025-06-01T16:15:00Z",
      "duration_s": 6298,
      "max_speed_kmh": 87.4,
      "max_lean_deg": 34.2,
      "max_rpm": 6200,
      "crash_detected": false,
      "distance_km": 45.2
    }
  ],
  "total": 12,
  "limit": 20,
  "offset": 0
}
```

---

### GET /sessions/:id

Get single session with summary stats.

**Response 200:**
```json
{
  "id": "550e8400-e29b-41d4-a716-446655440000",
  "device_id": "trak-cb350-001",
  "started_at": "2025-06-01T14:30:22Z",
  "ended_at": "2025-06-01T16:15:00Z",
  "duration_s": 6298,
  "distance_km": 45.2,
  "max_speed_kmh": 87.4,
  "max_lean_deg": 34.2,
  "max_rpm": 6200,
  "crash_detected": false,
  "crash_at": null,
  "total_readings": 62980,
  "total_batches": 210
}
```

**Response 404:**
```json
{ "error": "session_not_found", "message": "...", "status": 404 }
```

---

### GET /sessions/:id/telemetry

Raw telemetry for a session. Paginated — do not return all rows at once.

**Query params:**
| Param | Type | Default | Description |
|---|---|---|---|
| from | ISO8601 | session start | Time range start |
| to | ISO8601 | session end | Time range end |
| fields | csv string | all | Comma-separated fields to return |
| limit | int | 1000 | Max readings |
| offset | int | 0 | Pagination |
| resolution | string | raw | `raw`, `1s`, `10s`, `1m` — downsample |

**Response 200:**
```json
{
  "session_id": "550e8400-...",
  "from": "2025-06-01T14:30:22Z",
  "to": "2025-06-01T14:31:00Z",
  "resolution": "raw",
  "readings": [
    {
      "ts": "2025-06-01T14:30:22.000Z",
      "rpm": 1240,
      "speed_kmh": 0.0,
      "throttle": 12.1,
      "lean": 1.2,
      "g_total": 1.01
    }
  ],
  "count": 380,
  "total": 62980
}
```

---

### GET /sessions/:id/events

Events for a session (crashes, etc.).

**Response 200:**
```json
{
  "events": [
    {
      "id": 1,
      "ts": "2025-06-01T15:22:11Z",
      "type": "crash",
      "payload": {
        "g_total": 6.8,
        "speed_kmh": 2.1,
        "lean": -45.2
      }
    }
  ]
}
```

---

### GET /analytics/rpm-distribution/:session_id

RPM band distribution for a session.

**Response 200:**
```json
{
  "session_id": "550e8400-...",
  "bands": {
    "idle":    { "seconds": 320.4, "percent": 5.1 },
    "cruise":  { "seconds": 2840.0, "percent": 45.1 },
    "pull":    { "seconds": 2100.0, "percent": 33.3 },
    "redline": { "seconds": 1037.6, "percent": 16.5 }
  },
  "total_seconds": 6298
}
```

---

### GET /analytics/lean-histogram/:session_id

Lean angle distribution.

**Response 200:**
```json
{
  "session_id": "550e8400-...",
  "buckets": [
    { "range": "0-5°",   "left_seconds": 1200, "right_seconds": 1100 },
    { "range": "5-15°",  "left_seconds": 800,  "right_seconds": 750  },
    { "range": "15-30°", "left_seconds": 200,  "right_seconds": 180  },
    { "range": "30-45°", "left_seconds": 40,   "right_seconds": 28   },
    { "range": "45°+",   "left_seconds": 0,    "right_seconds": 0    }
  ],
  "max_lean_left": -38.4,
  "max_lean_right": 34.2
}
```

---

### GET /analytics/braking-events/:session_id

Hard braking events above threshold.

**Query params:**
| Param | Default | Description |
|---|---|---|
| threshold | -0.4 | Longitudinal G threshold (negative = braking) |

**Response 200:**
```json
{
  "session_id": "550e8400-...",
  "threshold_g": -0.4,
  "events": [
    {
      "ts": "2025-06-01T14:52:11Z",
      "speed_before_kmh": 67.2,
      "min_ax": -0.82,
      "duration_ms": 1400
    }
  ],
  "count": 8
}
```

---

### GET /analytics/session-compare

Compare stats across multiple sessions.

**Query params:**
| Param | Description |
|---|---|
| ids | Comma-separated session UUIDs (max 10) |

**Response 200:**
```json
{
  "sessions": [
    {
      "id": "550e8400-...",
      "started_at": "2025-06-01T14:30:22Z",
      "max_lean": 34.2,
      "max_speed": 87.4,
      "max_rpm": 6200,
      "avg_rpm": 2840,
      "distance_km": 45.2,
      "hard_braking_count": 8,
      "crash_detected": false
    }
  ]
}
```

---

### GET /devices/:id/status

Latest device heartbeat status.

**Response 200:**
```json
{
  "device_id": "trak-cb350-001",
  "last_seen": "2025-06-01T16:15:00Z",
  "online": false,
  "wifi_rssi": -62,
  "free_heap_bytes": 180000,
  "sd_free_mb": 7340,
  "unsynced_batches": 0,
  "battery_voltage": 12.6,
  "firmware": "1.0.0"
}
```

---

## Hono Route Structure

```typescript
import { Hono } from 'hono'
import { zValidator } from '@hono/zod-validator'

const app = new Hono()

app.use('*', authMiddleware)         // API key validation on all routes

app.get('/health', healthHandler)

const sessions = new Hono()
sessions.get('/',      listSessionsHandler)
sessions.get('/:id',   getSessionHandler)
sessions.get('/:id/telemetry', getSessionTelemetryHandler)
sessions.get('/:id/events',    getSessionEventsHandler)

const analytics = new Hono()
analytics.get('/rpm-distribution/:id',  rpmDistributionHandler)
analytics.get('/lean-histogram/:id',    leanHistogramHandler)
analytics.get('/braking-events/:id',    brakingEventsHandler)
analytics.get('/session-compare',       sessionCompareHandler)

const devices = new Hono()
devices.get('/:id/status', deviceStatusHandler)

app.route('/sessions',  sessions)
app.route('/analytics', analytics)
app.route('/devices',   devices)

export default app
```
