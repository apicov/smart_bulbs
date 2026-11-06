import { EventEmitter } from 'node:events';
import { act, render, screen, waitFor } from '@testing-library/react';
import userEvent from '@testing-library/user-event';

// A fake MQTT client: records publishes, lets the test deliver broker messages
class FakeClient extends EventEmitter {
  connected = false;
  published: { topic: string; payload: string; qos?: number }[] = [];
  subscribed: string[] = [];
  subscribe(t: string[]) { this.subscribed.push(...t); }
  publish(topic: string, payload: string, opts?: { qos?: number }) { this.published.push({ topic, payload, qos: opts?.qos }); }
  end() { this.connected = false; }
  deliver(topic: string, payload: string) { this.emit('message', topic, new TextEncoder().encode(payload)); }
}
const clients: { url: string; opts: Record<string, unknown>; client: FakeClient }[] = [];
vi.mock('mqtt', () => ({
  default: {
    connect: (url: string, opts: Record<string, unknown>) => {
      const client = new FakeClient();
      clients.push({ url, opts, client });
      return client;
    },
  },
}));

// Saved settings, as Capacitor Preferences (localStorage on the web) would return them
let saved: string | null = null;
vi.mock('@capacitor/preferences', () => ({
  Preferences: {
    get: async () => ({ value: saved }),
    set: async ({ value }: { value: string }) => { saved = value; },
    remove: async () => { saved = null; },
  },
}));

import App from './App';

beforeEach(() => {
  clients.length = 0;
  saved = null;
  window.location.hash = '#/';
});

it('asks for the password before connecting', async () => {
  render(<App />);
  expect(await screen.findByText(/enter the broker password/i)).toBeInTheDocument();
  expect(clients).toHaveLength(0);
});

it('lists bulbs from retained messages and switches one off', async () => {
  saved = JSON.stringify({ password: 'secret' });
  render(<App />);
  await waitFor(() => expect(clients).toHaveLength(1));
  const { url, opts, client } = clients[clients.length - 1];
  expect(url).toBe('wss://mqtt.apicov.xyz:8084');
  expect(opts).toMatchObject({ username: 'app', password: 'secret' });

  act(() => {
    client.connected = true;
    client.emit('connect');
    client.deliver('pico/bulb/living-room/status', 'online');
    client.deliver('pico/bulb/living-room/state', '{"state":"ON","brightness":128,"color_mode":"rgb","color":{"r":255,"g":180,"b":100}}');
  });
  expect(client.subscribed).toEqual(['pico/bulb/+/status', 'pico/bulb/+/state', 'pico/bulb/+/motion', 'pico/bulb/+/schedule']);
  expect(await screen.findByText('Living room')).toBeInTheDocument();
  expect(screen.getByText('On · 50%')).toBeInTheDocument();

  await userEvent.click(screen.getByRole('switch', { name: 'Living room power' }));
  expect(client.published).toEqual([{ topic: 'pico/bulb/living-room/set', payload: '{"state":"OFF"}', qos: 1 }]);
  // optimistic: shown as off before the bulb answers
  expect(screen.getByText('Off')).toBeInTheDocument();
});

it('shows a bulb as offline from its Last Will', async () => {
  saved = JSON.stringify({ password: 'secret' });
  render(<App />);
  await waitFor(() => expect(clients).toHaveLength(1));
  const { client } = clients[clients.length - 1];
  act(() => {
    client.connected = true;
    client.emit('connect');
    client.deliver('pico/bulb/kitchen/status', 'offline');
  });
  expect(await screen.findByText('Offline')).toBeInTheDocument();
  expect(screen.getByRole('switch', { name: 'Kitchen power' })).toBeDisabled();
});

describe('sleep timer and alarm', () => {
  const ALARM = '{"enabled":false,"hour":7,"minute":0,"days":[],"rise":1800,"brightness":255,"color":{"r":255,"g":180,"b":100}}';

  async function openBulb(timer = 'null', on = true) {
    saved = JSON.stringify({ password: 'secret' });
    window.location.hash = '#/bulb/lamp';
    render(<App />);
    await waitFor(() => expect(clients).toHaveLength(1));
    const { client } = clients[clients.length - 1];
    act(() => {
      client.connected = true;
      client.emit('connect');
      client.deliver('pico/bulb/lamp/status', 'online');
      client.deliver('pico/bulb/lamp/state', `{"state":"${on ? 'ON' : 'OFF'}","brightness":128,"color":{"r":255,"g":180,"b":100}}`);
      client.deliver('pico/bulb/lamp/schedule', `{"time_synced":true,"timer":${timer},"alarm":${ALARM}}`);
    });
    return client;
  }

  it('starts a timer that turns off suddenly by default', async () => {
    const client = await openBulb();
    await userEvent.click(await screen.findByRole('button', { name: 'Start timer' }));
    expect(client.published).toEqual([{ topic: 'pico/bulb/lamp/schedule_set', payload: '{"timer":{"after":1800,"fade":0}}', qos: 1 }]);
  });

  it('starts a timer with the fade time you choose', async () => {
    const client = await openBulb();
    await userEvent.click(await screen.findByRole('button', { name: 'Fade out' }));
    const fade = screen.getByRole('spinbutton', { name: 'Fade time (seconds)' });
    await userEvent.clear(fade);
    await userEvent.type(fade, '90');
    await userEvent.click(screen.getByRole('button', { name: 'Start timer' }));
    expect(client.published).toEqual([{ topic: 'pico/bulb/lamp/schedule_set', payload: '{"timer":{"after":1800,"fade":90}}', qos: 1 }]);
  });

  it('needs the light on before starting a timer', async () => {
    await openBulb('null', false);
    expect(await screen.findByRole('button', { name: 'Start timer' })).toBeDisabled();
    expect(screen.getByText(/turn the light on/i)).toBeInTheDocument();
  });

  it('shows a running timer and cancels it', async () => {
    const ends = Math.floor(Date.now() / 1000) + 125;
    const client = await openBulb(`{"ends":${ends},"fade":0}`);
    expect(await screen.findByText(/Off in 2:0[0-5]/)).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Cancel timer' }));
    expect(client.published).toEqual([{ topic: 'pico/bulb/lamp/schedule_set', payload: '{"timer":null}', qos: 1 }]);
  });

  it('saves an alarm for the chosen days', async () => {
    const client = await openBulb();
    await userEvent.click(await screen.findByRole('switch', { name: 'Alarm on' }));
    await userEvent.click(screen.getByRole('button', { name: 'Mon' }));
    await userEvent.click(screen.getByRole('button', { name: 'Fri' }));
    await userEvent.click(screen.getByRole('button', { name: 'Save alarm' }));
    expect(client.published).toHaveLength(1);
    expect(client.published[0].topic).toBe('pico/bulb/lamp/schedule_set');
    expect(JSON.parse(client.published[0].payload)).toEqual({
      alarm: { enabled: true, hour: 7, minute: 0, days: [1, 5], rise: 1800, brightness: 255, color: { r: 255, g: 180, b: 100 } },
    });
    // optimistic: the form keeps showing what was saved
    expect(screen.getByRole('switch', { name: 'Alarm on' })).toBeChecked();
  });

  it('warns when the bulb has no clock', async () => {
    saved = JSON.stringify({ password: 'secret' });
    window.location.hash = '#/bulb/lamp';
    render(<App />);
    await waitFor(() => expect(clients).toHaveLength(1));
    const { client } = clients[clients.length - 1];
    act(() => {
      client.connected = true;
      client.emit('connect');
      client.deliver('pico/bulb/lamp/status', 'online');
      client.deliver('pico/bulb/lamp/state', '{"state":"ON","brightness":128,"color":{"r":255,"g":180,"b":100}}');
      client.deliver('pico/bulb/lamp/schedule', `{"time_synced":false,"timer":null,"alarm":${ALARM}}`);
    });
    expect(await screen.findByText(/doesn't know the time yet/i)).toBeInTheDocument();
  });
});
