import { createContext, useContext, useEffect, useMemo, useRef, useState, type ReactNode } from 'react';
import { getPref, setPref } from '@/services/prefs';

export type ThemeMode = 'system' | 'light' | 'dark';

export interface Settings {
  brokerUrl: string; // wss://host:port — browsers can only speak MQTT over WebSockets
  username: string;
  password: string;
  theme: ThemeMode;
}

export const DEFAULT_SETTINGS: Settings = {
  brokerUrl: 'wss://mqtt.apicov.xyz:8084',
  username: 'app',
  password: '',
  theme: 'system',
};

const KEY = 'settings';

interface SettingsState {
  settings: Settings;
  /** false until the saved settings have been read (don't connect with the defaults meanwhile) */
  loaded: boolean;
  update: (patch: Partial<Settings>) => void;
}

const SettingsContext = createContext<SettingsState | null>(null);

export function SettingsProvider({ children }: { children: ReactNode }) {
  const [settings, setSettings] = useState<Settings>(DEFAULT_SETTINGS);
  const [loaded, setLoaded] = useState(false);
  // changes made before the saved settings arrived win over them
  const changes = useRef<Partial<Settings> | null>(null);

  useEffect(() => {
    void getPref<Partial<Settings>>(KEY).then((saved) => {
      if (saved) setSettings({ ...DEFAULT_SETTINGS, ...saved, ...changes.current });
      setLoaded(true);
    });
  }, []);
  useEffect(() => {
    if (changes.current) void setPref(KEY, settings);
  }, [settings]);

  const value = useMemo<SettingsState>(() => ({
    settings,
    loaded,
    update: (patch) => {
      changes.current = { ...changes.current, ...patch };
      setSettings((s) => ({ ...s, ...patch }));
    },
  }), [settings, loaded]);

  return <SettingsContext.Provider value={value}>{children}</SettingsContext.Provider>;
}

export function useSettings(): SettingsState {
  const ctx = useContext(SettingsContext);
  if (!ctx) throw new Error('useSettings outside SettingsProvider');
  return ctx;
}
