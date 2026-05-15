import { db } from '../db/client'
import { deviceStatus, devices } from '../db/schema'
import { eq } from 'drizzle-orm'

export interface StatusPayload {
  v:               number
  device_id:       string
  session_id:      string
  timestamp:       number
  uptime_ms:       number
  wifi_rssi:       number
  free_heap_bytes: number
  sd_free_mb:      number
  unsynced_batches: number
  obd_connected:   boolean
  imu_ok:          boolean
  chip_temp:       number
  firmware:        string
}

export async function handleStatusUpdate(payload: StatusPayload): Promise<void> {
  const { device_id } = payload

  // Ensure device exists
  await db.insert(devices)
    .values({ id: device_id, name: device_id, apiKey: `trak_dev_${device_id}` })
    .onConflictDoNothing()

  // Upsert device_status — one row per device, always latest heartbeat
  await db.insert(deviceStatus)
    .values({
      deviceId:        device_id,
      lastSeen:        new Date(payload.timestamp),
      sessionId:       payload.session_id,
      uptimeMs:        payload.uptime_ms,
      wifiRssi:        payload.wifi_rssi,
      freeHeapBytes:   payload.free_heap_bytes,
      sdFreeMb:        payload.sd_free_mb,
      unsyncedBatches: payload.unsynced_batches,
      obdConnected:    payload.obd_connected,
      imuOk:           payload.imu_ok,
      chipTemp:        payload.chip_temp,
      firmware:        payload.firmware,
    })
    .onConflictDoUpdate({
      target: deviceStatus.deviceId,
      set: {
        lastSeen:        new Date(payload.timestamp),
        sessionId:       payload.session_id,
        uptimeMs:        payload.uptime_ms,
        wifiRssi:        payload.wifi_rssi,
        freeHeapBytes:   payload.free_heap_bytes,
        sdFreeMb:        payload.sd_free_mb,
        unsyncedBatches: payload.unsynced_batches,
        obdConnected:    payload.obd_connected,
        imuOk:           payload.imu_ok,
        chipTemp:        payload.chip_temp,
        firmware:        payload.firmware,
      },
    })

  console.log(`[status] ${device_id} — IMU:${payload.imu_ok} OBD:${payload.obd_connected} heap:${payload.free_heap_bytes}`)
}
