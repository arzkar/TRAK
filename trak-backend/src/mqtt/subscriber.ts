import mqtt from 'mqtt'
import { handleTelemetryBatch, type TelemetryBatch } from '../handlers/telemetry'
import { handleCrashEvent,   type CrashPayload   } from '../handlers/crash'
import { handleStatusUpdate, type StatusPayload   } from '../handlers/status'

const BROKER = process.env.MQTT_BROKER ?? 'localhost'
const PORT   = parseInt(process.env.MQTT_PORT ?? '1883')

export function startMQTTSubscriber(): void {
  const client = mqtt.connect(`mqtt://${BROKER}:${PORT}`, {
    clientId:  `trak-backend-${Math.random().toString(16).slice(2, 8)}`,
    keepalive: 60,
    reconnectPeriod: 3000,
  })

  client.on('connect', () => {
    console.log(`[MQTT] Connected to broker at ${BROKER}:${PORT}`)
    client.subscribe('trak/+/telemetry', { qos: 1 })
    client.subscribe('trak/+/crash',     { qos: 1 })
    client.subscribe('trak/+/status',    { qos: 0 })
    console.log('[MQTT] Subscribed to trak/+/{telemetry,crash,status}')
  })

  client.on('reconnect', () => console.log('[MQTT] Reconnecting...'))
  client.on('error', (err) => console.error('[MQTT] Error:', err.message))

  client.on('message', async (topic: string, payload: Buffer) => {
    const parts = topic.split('/')                  // ['trak', deviceId, type]
    const msgType = parts[2] as 'telemetry' | 'crash' | 'status'

    let body: any;
    try {
      const raw = payload.toString();
      body = JSON.parse(raw);
      
      // Print the JSON
      if (msgType === 'telemetry') {
        const sample = body.readings?.[0] || {};
        console.log(`[MQTT] Telemetry Batch ${body.batch_index} from ${parts[1]}`);
        console.log(`[MQTT] Sample Reading [0]:`, JSON.stringify(sample, null, 2));
        console.log(`[MQTT] Total Readings in Batch: ${body.reading_count}`);
      } else {
        console.log(`[MQTT] Payload on ${topic}:`, JSON.stringify(body, null, 2));
      }
    } catch {
      console.error(`[MQTT] Invalid JSON on topic ${topic}`);
      return;
    }

    try {
      switch (msgType) {
        case 'telemetry':
          await handleTelemetryBatch(body as TelemetryBatch)
          break
        case 'crash':
          await handleCrashEvent(body as CrashPayload)
          break
        case 'status':
          await handleStatusUpdate(body as StatusPayload)
          break
        default:
          console.warn(`[MQTT] Unknown message type: ${msgType}`)
      }
    } catch (err) {
      console.error(`[MQTT] Handler error for ${topic}:`, err)
    }
  })
}
