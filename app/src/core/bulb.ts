/**
 * The bulb protocol, as pure functions (no React, no MQTT client): topics, payloads, and how incoming
 * messages update the app's map of bulbs. Must match firmware/components/bulb_ctrl and broker/README.md.
 *
 * Topics: <prefix>/<id>/status | state | set | motion | schedule | schedule_set
 * Payloads follow Home Assistant's MQTT JSON light schema.
 */

export const TOPIC_PREFIX = 'pico/bulb';

export interface Rgb { r: number; g: number; b: number }

/** What a bulb reports on `state` (retained). */
export interface BulbState {
  on: boolean;
  brightness: number; // 1–255
  color: Rgb;
}

/** A running sleep timer: the light is fully off at `ends` (Unix seconds, 0 until the bulb's clock is set). */
export interface SleepTimer {
  ends: number;
  fade: number; // seconds
}

/** A wake-up alarm: reaches brightness at hour:minute, starting `rise` seconds earlier. */
export interface Alarm {
  enabled: boolean;
  hour: number;
  minute: number;
  days: number[]; // 0 = Sunday … 6 = Saturday. Empty = once.
  rise: number; // seconds
  brightness: number; // 1–255
  color: Rgb;
}

/** What a bulb reports on `schedule` (retained). */
export interface Schedule {
  timeSynced: boolean;
  timer: SleepTimer | null;
  alarm: Alarm;
}

/** A `schedule_set` command. `timer: null` cancels. A fade of 0 turns off suddenly. */
export interface ScheduleCommand {
  timer?: { after: number; fade: number } | null;
  alarm?: Partial<Alarm>;
}

export interface Bulb {
  id: string;
  online: boolean;
  state: BulbState | null; // null until its first `state` message
  motion: boolean;
  schedule: Schedule | null; // null until its first `schedule` message
}

/** A `set` command: every field optional. `transition` is in seconds. */
export interface BulbCommand {
  on?: boolean;
  brightness?: number;
  color?: Rgb;
  transition?: number;
}

export type Bulbs = Record<string, Bulb>;

export const topics = {
  /** what the app subscribes to: every bulb's retained status/state/motion/schedule */
  subscribe: [
    `${TOPIC_PREFIX}/+/status`,
    `${TOPIC_PREFIX}/+/state`,
    `${TOPIC_PREFIX}/+/motion`,
    `${TOPIC_PREFIX}/+/schedule`,
  ],
  set: (id: string) => `${TOPIC_PREFIX}/${id}/set`,
  scheduleSet: (id: string) => `${TOPIC_PREFIX}/${id}/schedule_set`,
};

const clamp255 = (n: unknown) => Math.max(0, Math.min(255, Math.round(Number(n) || 0)));

/** `pico/bulb/<id>/<leaf>` → { id, leaf }, or null for anything else. */
export function parseTopic(topic: string): { id: string; leaf: string } | null {
  const parts = topic.split('/');
  const prefix = TOPIC_PREFIX.split('/');
  if (parts.length !== prefix.length + 2 || prefix.some((p, i) => parts[i] !== p)) return null;
  const id = parts[prefix.length];
  return id ? { id, leaf: parts[prefix.length + 1] } : null;
}

/** The firmware's `state` JSON → BulbState (null if it isn't one). */
export function parseState(payload: string): BulbState | null {
  try {
    const j = JSON.parse(payload) as { state?: unknown; brightness?: unknown; color?: Partial<Rgb> };
    if (typeof j !== 'object' || j === null || typeof j.state !== 'string') return null;
    const c = j.color ?? {};
    return {
      on: j.state.toUpperCase() === 'ON',
      brightness: Math.max(1, clamp255(j.brightness ?? 255)),
      color: { r: clamp255(c.r), g: clamp255(c.g), b: clamp255(c.b) },
    };
  } catch {
    return null;
  }
}

/** BulbCommand → the `set` JSON the firmware expects. */
export function encodeCommand(cmd: BulbCommand): string {
  const out: Record<string, unknown> = {};
  if (cmd.on !== undefined) out.state = cmd.on ? 'ON' : 'OFF';
  if (cmd.brightness !== undefined) out.brightness = Math.max(1, clamp255(cmd.brightness));
  if (cmd.color) out.color = { r: clamp255(cmd.color.r), g: clamp255(cmd.color.g), b: clamp255(cmd.color.b) };
  if (cmd.transition !== undefined) out.transition = Math.max(0, cmd.transition);
  return JSON.stringify(out);
}

/** The firmware's `schedule` JSON → Schedule (null if it isn't one). */
export function parseSchedule(payload: string): Schedule | null {
  try {
    const j = JSON.parse(payload) as {
      time_synced?: unknown;
      timer?: { ends?: unknown; fade?: unknown } | null;
      alarm?: Partial<Alarm>;
    };
    if (typeof j !== 'object' || j === null || typeof j.alarm !== 'object' || j.alarm === null) return null;
    const a = j.alarm;
    const c = a.color ?? ({} as Partial<Rgb>);
    return {
      timeSynced: j.time_synced === true,
      timer: j.timer ? { ends: Number(j.timer.ends) || 0, fade: Number(j.timer.fade) || 0 } : null,
      alarm: {
        enabled: a.enabled === true,
        hour: Number(a.hour) || 0,
        minute: Number(a.minute) || 0,
        days: Array.isArray(a.days) ? a.days.map(Number) : [],
        rise: Number(a.rise) || 0,
        brightness: Math.max(1, clamp255(a.brightness ?? 255)),
        color: { r: clamp255(c.r), g: clamp255(c.g), b: clamp255(c.b) },
      },
    };
  } catch {
    return null;
  }
}

/** Apply one incoming message to the map of bulbs. Returns the same object if nothing changed. */
export function applyMessage(bulbs: Bulbs, topic: string, payload: string): Bulbs {
  const t = parseTopic(topic);
  if (!t) return bulbs;
  const prev: Bulb = bulbs[t.id] ?? { id: t.id, online: false, state: null, motion: false, schedule: null };

  // An empty retained message clears the topic: the bulb was removed from the broker
  if (payload === '' && t.leaf === 'status') {
    if (!(t.id in bulbs)) return bulbs;
    const { [t.id]: _gone, ...rest } = bulbs;
    return rest;
  }

  let next: Bulb = prev;
  switch (t.leaf) {
    case 'status':
      next = { ...prev, online: payload === 'online' };
      break;
    case 'state': {
      const s = parseState(payload);
      if (s) next = { ...prev, state: s };
      break;
    }
    case 'motion':
      try { next = { ...prev, motion: Boolean((JSON.parse(payload) as { motion?: unknown }).motion) }; }
      catch { /* ignore malformed */ }
      break;
    case 'schedule': {
      const s = parseSchedule(payload);
      if (s) next = { ...prev, schedule: s };
      break;
    }
    default:
      return bulbs;
  }
  return next === prev && t.id in bulbs ? bulbs : { ...bulbs, [t.id]: next };
}

/** Optimistically apply a command we just sent, so the UI doesn't wait for the round trip.
 *  Mirrors the firmware: only `on` changes power (the UI sends on:true with a colour pick). */
export function applyCommand(state: BulbState | null, cmd: BulbCommand): BulbState | null {
  if (!state) return state;
  return {
    on: cmd.on ?? state.on,
    brightness: cmd.brightness !== undefined ? Math.max(1, clamp255(cmd.brightness)) : state.brightness,
    color: cmd.color ?? state.color,
  };
}

export const toHex = ({ r, g, b }: Rgb) =>
  '#' + [r, g, b].map((v) => clamp255(v).toString(16).padStart(2, '0')).join('');

export function fromHex(hex: string): Rgb | null {
  const m = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(hex.trim());
  return m ? { r: parseInt(m[1], 16), g: parseInt(m[2], 16), b: parseInt(m[3], 16) } : null;
}

/** "living-room" → "Living room" */
export const displayName = (id: string) => {
  const s = id.replace(/[-_]+/g, ' ').trim();
  return s.charAt(0).toUpperCase() + s.slice(1);
};

/** One-tap colours. Whites first: they're what a lamp shows most of the time. */
export const PALETTE: { name: string; color: Rgb }[] = [
  { name: 'Candle', color: { r: 255, g: 130, b: 30 } },
  { name: 'Warm white', color: { r: 255, g: 180, b: 100 } },
  { name: 'Neutral white', color: { r: 255, g: 220, b: 170 } },
  { name: 'Cool white', color: { r: 255, g: 255, b: 255 } },
  { name: 'Red', color: { r: 255, g: 0, b: 0 } },
  { name: 'Orange', color: { r: 255, g: 80, b: 0 } },
  { name: 'Yellow', color: { r: 255, g: 200, b: 0 } },
  { name: 'Green', color: { r: 0, g: 255, b: 40 } },
  { name: 'Cyan', color: { r: 0, g: 220, b: 255 } },
  { name: 'Blue', color: { r: 0, g: 40, b: 255 } },
  { name: 'Purple', color: { r: 140, g: 0, b: 255 } },
  { name: 'Pink', color: { r: 255, g: 0, b: 120 } },
];
