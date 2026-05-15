import { Hono } from 'hono'
import { db } from '../db/client'
import { devices, deviceStatus } from '../db/schema'
import { eq } from 'drizzle-orm'

export const devicesRouter = new Hono()

// GET /devices/:id/status
devicesRouter.get('/:id/status', async (c) => {
  const id = c.req.param('id')

  const [status] = await db.select().from(deviceStatus)
    .where(eq(deviceStatus.deviceId, id))

  if (!status) return c.json({ error: 'device_not_found', status: 404 }, 404)

  // Consider online if last seen within 2 minutes
  const online = status.lastSeen
    ? (Date.now() - status.lastSeen.getTime()) < 2 * 60 * 1000
    : false

  return c.json({ ...status, online })
})
