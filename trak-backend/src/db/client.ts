import postgres from 'postgres'
import { drizzle } from 'drizzle-orm/postgres-js'
import * as schema from './schema'

if (!process.env.DATABASE_URL) {
  throw new Error('DATABASE_URL is not set')
}

const sql = postgres(process.env.DATABASE_URL, {
  max: 10,        // connection pool size
  idle_timeout: 30,
})

export const db = drizzle(sql, { schema })

export type DB = typeof db
