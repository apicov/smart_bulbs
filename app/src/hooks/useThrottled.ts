import { useCallback, useEffect, useRef } from 'react';

/**
 * Rate-limit a callback (leading + trailing): the first call runs at once, later ones at most every `ms`,
 * and the last value is never lost. `flush` cancels anything pending, for the final value of a drag.
 */
export function useThrottled<A extends unknown[]>(fn: (...args: A) => void, ms: number) {
  const fnRef = useRef(fn);
  const last = useRef(0);
  const pending = useRef<A | null>(null);
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null);

  useEffect(() => { fnRef.current = fn; }, [fn]);
  useEffect(() => () => { if (timer.current) clearTimeout(timer.current); }, []);

  const call = useCallback((...args: A) => {
    const wait = last.current + ms - Date.now();
    if (wait <= 0) {
      last.current = Date.now();
      fnRef.current(...args);
      return;
    }
    pending.current = args;
    timer.current ??= setTimeout(() => {
      timer.current = null;
      if (pending.current) {
        last.current = Date.now();
        fnRef.current(...pending.current);
        pending.current = null;
      }
    }, wait);
  }, [ms]);

  const cancel = useCallback(() => {
    if (timer.current) clearTimeout(timer.current);
    timer.current = null;
    pending.current = null;
  }, []);

  return { call, cancel };
}
