import { Hono } from 'hono'
import { db } from '../db/client'
import { telemetry, sessions } from '../db/schema'
import { eq, and, sql } from 'drizzle-orm'

export const analyticsRouter = new Hono()

// GET /analytics/rpm-distribution/:session_id
analyticsRouter.get('/rpm-distribution/:id', async (c) => {
  const id = c.req.param('id')

  const rows = await db.execute(sql`
    SELECT
      CASE
        WHEN rpm < 1200 THEN 'idle'
        WHEN rpm < 3000 THEN 'cruise'
        WHEN rpm < 5500 THEN 'pull'
        ELSE 'redline'
      END AS band,
      COUNT(*) * 0.1 AS seconds,
      ROUND(COUNT(*) * 100.0 / SUM(COUNT(*)) OVER (), 1) AS percent
    FROM ${telemetry}
    WHERE session_id = ${id}::uuid
      AND rpm IS NOT NULL
    GROUP BY 1
    ORDER BY MIN(rpm)
  `)

  return c.json({ session_id: id, bands: rows })
})

// GET /analytics/lean-histogram/:session_id
analyticsRouter.get('/lean-histogram/:id', async (c) => {
  const id = c.req.param('id')

  const rows = await db.execute(sql`
    SELECT
      CASE
        WHEN lean BETWEEN  0  AND  5  THEN '0-5° right'
        WHEN lean BETWEEN  5  AND 15  THEN '5-15° right'
        WHEN lean BETWEEN 15  AND 30  THEN '15-30° right'
        WHEN lean > 30                THEN '30°+ right'
        WHEN lean BETWEEN -5  AND  0  THEN '0-5° left'
        WHEN lean BETWEEN -15 AND -5  THEN '5-15° left'
        WHEN lean BETWEEN -30 AND -15 THEN '15-30° left'
        WHEN lean < -30               THEN '30°+ left'
      END AS band,
      COUNT(*) * 0.1 AS seconds
    FROM ${telemetry}
    WHERE session_id = ${id}::uuid AND lean IS NOT NULL
    GROUP BY 1
  `)

  return c.json({ session_id: id, buckets: rows })
})

// GET /analytics/braking-events/:session_id?threshold=-0.4
analyticsRouter.get('/braking-events/:id', async (c) => {
  const id        = c.req.param('id')
  const threshold = parseFloat(c.req.query('threshold') ?? '-0.4')

  const rows = await db.execute(sql`
    SELECT ts, speed_kmh, ax AS longitudinal_g, g_total
    FROM ${telemetry}
    WHERE session_id = ${id}::uuid
      AND ax < ${threshold}
      AND speed_kmh > 10
    ORDER BY ax ASC
  `)

  return c.json({ session_id: id, threshold_g: threshold, events: rows, count: rows.length })
})

// GET /analytics/session-compare?ids=uuid1,uuid2
analyticsRouter.get('/session-compare', async (c) => {
  const ids = (c.req.query('ids') ?? '').split(',').filter(Boolean)
  if (!ids.length) return c.json({ error: 'ids query param required', status: 400 }, 400)

  const rows = await db.execute(sql`
    SELECT
      s.id, s.started_at, s.distance_km,
      s.max_speed_kmh, s.max_lean_deg, s.max_rpm, s.crash_detected,
      COUNT(CASE WHEN t.ax < -0.4 THEN 1 END) AS hard_braking_count,
      COUNT(CASE WHEN t.ax >  0.3 THEN 1 END) AS hard_accel_count,
      AVG(t.rpm) AS avg_rpm
    FROM ${sessions} s
    JOIN ${telemetry} t ON t.session_id = s.id
    WHERE s.id = ANY(${ids}::uuid[])
    GROUP BY s.id, s.started_at, s.distance_km,
             s.max_speed_kmh, s.max_lean_deg, s.max_rpm, s.crash_detected
    ORDER BY s.started_at DESC
  `)

  return c.json({ sessions: rows })
})
