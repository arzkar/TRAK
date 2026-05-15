import type { Context, Next } from 'hono'

const DEV_KEY = process.env.API_KEY ?? 'trak_dev_key_change_me'

export async function authMiddleware(c: Context, next: Next) {
  // Health check is public
  if (c.req.path === '/health') return next()

  const key = c.req.header('X-API-Key')
  if (!key || key !== DEV_KEY) {
    return c.json({ error: 'unauthorized', message: 'Invalid API key', status: 401 }, 401)
  }
  return next()
}
