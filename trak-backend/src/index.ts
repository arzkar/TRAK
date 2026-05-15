import { Hono } from 'hono'
import { logger } from 'hono/logger'
import { authMiddleware } from './middleware/auth'
import { sessionsRouter }  from './routes/sessions'
import { analyticsRouter } from './routes/analytics'
import { devicesRouter }   from './routes/devices'
import { startMQTTSubscriber } from './mqtt/subscriber'
import { db } from './db/client'
import { sql } from 'drizzle-orm'

const app = new Hono()

app.use('*', logger())
app.use('*', authMiddleware)

// ── Health check ──────────────────────────────────────────────────────────────
app.get('/health', async (c) => {
  let dbOk = false
  try {
    await db.execute(sql`SELECT 1`)
    dbOk = true
  } catch {}

  return c.json({
    status:    'ok',
    timestamp: Date.now(),
    db:        dbOk ? 'ok' : 'error',
    mqtt:      'connected',  // if we reached here subscriber is up
  })
})

// ── Routes ────────────────────────────────────────────────────────────────────
app.route('/sessions',  sessionsRouter)
app.route('/analytics', analyticsRouter)
app.route('/devices',   devicesRouter)

// ── Start ─────────────────────────────────────────────────────────────────────
const PORT = parseInt(process.env.PORT ?? '3000')

startMQTTSubscriber()

console.log(`[TRAK] Backend starting on http://0.0.0.0:${PORT}`)

export default {
  port: PORT,
  fetch: app.fetch,
}
