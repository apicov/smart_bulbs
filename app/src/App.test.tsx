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
  expect(client.subscribed).toEqual(['pico/bulb/+/status', 'pico/bulb/+/state', 'pico/bulb/+/motion']);
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
