import { Box } from '@mui/material';
import type { BulbState } from '@/core/bulb';

/** A round "lamp" in the bulb's current colour, glowing in proportion to its brightness; grey when off. */
export function BulbGlow({ state, size = 40 }: { state: BulbState | null; size?: number }) {
  const on = !!state?.on;
  const { r, g, b } = state?.color ?? { r: 128, g: 128, b: 128 };
  const k = on ? 0.35 + 0.65 * (state!.brightness / 255) : 0;
  const fill = on ? `rgb(${r}, ${g}, ${b})` : 'transparent';
  return (
    <Box
      aria-hidden
      sx={{
        width: size, height: size, borderRadius: '50%', flexShrink: 0,
        bgcolor: fill, opacity: on ? 0.4 + 0.6 * k : 1,
        border: on ? 'none' : '2px dashed', borderColor: 'divider',
        boxShadow: on ? `0 0 ${size * 0.6 * k}px ${size * 0.15 * k}px rgba(${r}, ${g}, ${b}, 0.6)` : 'none',
        transition: 'background-color 300ms, box-shadow 300ms, opacity 300ms',
      }}
    />
  );
}
