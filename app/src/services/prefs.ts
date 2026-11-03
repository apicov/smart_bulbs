/** Small JSON values that must survive restarts (Capacitor Preferences: SharedPreferences on Android,
 *  localStorage on the web). Books and dictionaries live elsewhere; this is for settings and caches
 *  of a few KB. */
import { Preferences } from '@capacitor/preferences';

export async function getPref<T>(key: string): Promise<T | null> {
  try {
    const { value } = await Preferences.get({ key });
    return value ? (JSON.parse(value) as T) : null;
  } catch {
    return null;
  }
}

export async function setPref(key: string, value: unknown): Promise<void> {
  if (value === null || value === undefined) await Preferences.remove({ key });
  else await Preferences.set({ key, value: JSON.stringify(value) });
}

const updating = new Map<string, Promise<unknown>>();

/** Read, change and write one value, one change at a time per key: two changes that overlap (a queue a flush
 *  takes from while the reader adds to it) would otherwise each write back their own copy and lose the other's. */
export function updatePref<T>(key: string, change: (value: T | null) => T | null): Promise<T | null> {
  const next = (updating.get(key) ?? Promise.resolve()).catch(() => undefined).then(async () => {
    const value = change(await getPref<T>(key));
    await setPref(key, value);
    return value;
  });
  updating.set(key, next);
  void next.finally(() => { if (updating.get(key) === next) updating.delete(key); }).catch(() => undefined);
  return next;
}
