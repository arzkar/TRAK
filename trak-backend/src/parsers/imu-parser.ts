// IMU derived values — computed in Hono, never in ESP32 (Option B).

/** Lean angle (roll): atan2(ay, az) in degrees. Positive = right lean. */
export function computeLean(ay: number, az: number): number {
  return Math.atan2(ay, az) * (180 / Math.PI)
}

/** Pitch angle: atan2(-ax, az) in degrees. Positive = nose up. */
export function computePitch(ax: number, az: number): number {
  return Math.atan2(-ax, az) * (180 / Math.PI)
}

/** G-force magnitude across all axes. */
export function computeGTotal(ax: number, ay: number, az: number): number {
  return Math.sqrt(ax * ax + ay * ay + az * az)
}
