import { Hono } from 'hono'
import { zValidator } from '@hono/zod-validator'
import { z } from 'zod'
import { db } from '../db/client'
import { sessions, telemetry, events } from '../db/schema'
import { eq, and, gte, lte, desc } from 'drizzle-orm'

export const sessionsRouter = new Hono()

// GET /sessions?device_id=&limit=20&offset=0
sessionsRouter.get('/', zValidator('query', z.object({
  device_id: z.string(),
  limit:  z.coerce.number().int().min(1).max(100).default(20),
  offset: z.coerce.number().int().min(0).default(0),
})), async (c) => {
  const { device_id, limit, offset } = c.req.valid('query')

  const rows = await db.select().from(sessions)
    .where(eq(sessions.deviceId, device_id))
    .orderBy(desc(sessions.startedAt))
    .limit(limit)
    .offset(offset)

  return c.json({ sessions: rows, limit, offset })
})

// GET /sessions/:id
sessionsRouter.get('/:id', async (c) => {
  const id = c.req.param('id')
  const [row] = await db.select().from(sessions).where(eq(sessions.id, id))
  if (!row) return c.json({ error: 'session_not_found', status: 404 }, 404)
  return c.json(row)
})

// GET /sessions/:id/telemetry?from=&to=&limit=1000&offset=0
sessionsRouter.get('/:id/telemetry', zValidator('query', z.object({
  from:   z.string().optional(),
  to:     z.string().optional(),
  limit:  z.coerce.number().int().min(1).max(5000).default(1000),
  offset: z.coerce.number().int().min(0).default(0),
})), async (c) => {
  const id = c.req.param('id')
  const { from, to, limit, offset } = c.req.valid('query')

  const filters = [eq(telemetry.sessionId, id)]
  if (from) filters.push(gte(telemetry.ts, new Date(from)))
  if (to)   filters.push(lte(telemetry.ts, new Date(to)))

  const rows = await db.select().from(telemetry)
    .where(and(...filters))
    .orderBy(telemetry.ts)
    .limit(limit)
    .offset(offset)

  return c.json({ session_id: id, readings: rows, count: rows.length })
})

// GET /sessions/:id/events
sessionsRouter.get('/:id/events', async (c) => {
  const id = c.req.param('id')
  const rows = await db.select().from(events)
    .where(eq(events.sessionId, id))
    .orderBy(events.ts)
  return c.json({ events: rows })
})
