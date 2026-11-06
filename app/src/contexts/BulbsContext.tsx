import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState, type ReactNode } from 'react';
import mqtt, { type MqttClient } from 'mqtt';
import { applyCommand, applyMessage, encodeCommand, topics, type BulbCommand, type Bulbs, type ScheduleCommand } from '@/core/bulb';
import { useSettings } from '@/contexts/SettingsContext';

export type ConnectionStatus =
  | { kind: 'unconfigured' } // no password yet
  | { kind: 'connecting' }
  | { kind: 'connected' }
  | { kind: 'offline'; message: string } // will retry by itself
  | { kind: 'denied'; message: string }; // wrong credentials: no retry until settings change

interface BulbsState {
  bulbs: Bulbs;
  status: ConnectionStatus;
  /** Send a command. qos 0 for the stream of values while dragging, 1 (default) for anything final. */
  send: (id: string, cmd: BulbCommand, qos?: 0 | 1) => void;
  /** Change a bulb's sleep timer or alarm. */
  sendSchedule: (id: string, cmd: ScheduleCommand) => void;
}

const BulbsContext = createContext<BulbsState | null>(null);
const decoder = new TextDecoder();

export function BulbsProvider({ children }: { children: ReactNode }) {
  const { settings, loaded } = useSettings();
  const { brokerUrl, username, password } = settings;
  const [bulbs, setBulbs] = useState<Bulbs>({});
  const [status, setStatus] = useState<ConnectionStatus>({ kind: 'connecting' });
  const client = useRef<MqttClient | null>(null);

  useEffect(() => {
    if (!loaded) return;
    setBulbs({}); // another broker or user: start from what it tells us
    if (!password || !brokerUrl) {
      setStatus({ kind: 'unconfigured' });
      return;
    }
    setStatus({ kind: 'connecting' });
    const c = mqtt.connect(brokerUrl, {
      username,
      password,
      clientId: `pico-app-${Math.random().toString(16).slice(2, 10)}`,
      clean: true,
      reconnectPeriod: 3000,
      connectTimeout: 10_000,
      keepalive: 30,
    });
    client.current = c;

    c.on('connect', () => {
      setStatus({ kind: 'connected' });
      // retained status/state/motion of every bulb arrive right after subscribing
      c.subscribe(topics.subscribe, { qos: 1 });
    });
    c.on('message', (topic, payload) => {
      const text = decoder.decode(payload);
      setBulbs((b) => applyMessage(b, topic, text));
    });
    c.on('error', (err) => {
      const code = (err as { code?: number }).code;
      // CONNACK 4/5 (MQTT 3.1.1) or 134/135 (MQTT 5): bad username/password or not authorised
      if (code === 4 || code === 5 || code === 134 || code === 135) {
        setStatus({ kind: 'denied', message: 'The broker refused the username or password.' });
        c.end(true);
      } else {
        setStatus({ kind: 'offline', message: err.message || 'Connection error' });
      }
    });
    c.on('offline', () => setStatus((s) => (s.kind === 'denied' ? s : { kind: 'offline', message: 'Broker unreachable, retrying…' })));
    c.on('reconnect', () => setStatus((s) => (s.kind === 'denied' ? s : { kind: 'connecting' })));

    return () => {
      client.current = null;
      c.end(true);
    };
  }, [loaded, brokerUrl, username, password]);

  const send = useCallback((id: string, cmd: BulbCommand, qos: 0 | 1 = 1) => {
    const c = client.current;
    if (!c?.connected) return;
    c.publish(topics.set(id), encodeCommand(cmd), { qos });
    // show it now; the bulb's retained `state` reply confirms or corrects it
    setBulbs((b) => (b[id] ? { ...b, [id]: { ...b[id], state: applyCommand(b[id].state, cmd) } } : b));
  }, []);

  const sendSchedule = useCallback((id: string, cmd: ScheduleCommand) => {
    const c = client.current;
    if (!c?.connected) return;
    c.publish(topics.scheduleSet(id), JSON.stringify(cmd), { qos: 1 });
    // show the alarm now; the bulb's retained `schedule` reply confirms or corrects it
    const alarm = cmd.alarm;
    if (alarm) {
      setBulbs((b) => {
        const schedule = b[id]?.schedule;
        return schedule ? { ...b, [id]: { ...b[id], schedule: { ...schedule, alarm: { ...schedule.alarm, ...alarm } } } } : b;
      });
    }
  }, []);

  const value = useMemo(() => ({ bulbs, status, send, sendSchedule }), [bulbs, status, send, sendSchedule]);
  return <BulbsContext.Provider value={value}>{children}</BulbsContext.Provider>;
}

export function useBulbs(): BulbsState {
  const ctx = useContext(BulbsContext);
  if (!ctx) throw new Error('useBulbs outside BulbsProvider');
  return ctx;
}
