import { useEffect, useState } from 'react';
import { Button, Card, CardContent, Stack, TextField, ToggleButton, ToggleButtonGroup, Typography } from '@mui/material';
import type { SleepTimer } from '@/core/bulb';

const MAX_MINUTES = 24 * 60;
const MAX_FADE_SECONDS = 3600;

function formatLeft(seconds: number): string {
  const s = Math.max(0, seconds);
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const ss = String(s % 60).padStart(2, '0');
  return h > 0 ? `${h}:${String(m).padStart(2, '0')}:${ss}` : `${m}:${ss}`;
}

interface Props {
  timer: SleepTimer | null;
  lightOn: boolean;
  disabled: boolean;
  onStart: (afterSeconds: number, fadeSeconds: number) => void;
  onCancel: () => void;
}

/** Turns the light off after a while, suddenly or with a fade. The bulb runs it, so the app can close. */
export function SleepTimerCard({ timer, lightOn, disabled, onStart, onCancel }: Props) {
  const [minutes, setMinutes] = useState('30');
  const [mode, setMode] = useState<'sudden' | 'fade'>('sudden');
  const [fadeSeconds, setFadeSeconds] = useState('60');
  const [now, setNow] = useState(() => Date.now());

  const running = timer !== null;
  useEffect(() => {
    if (!running) return;
    const tick = setInterval(() => setNow(Date.now()), 1000);
    return () => clearInterval(tick);
  }, [running]);

  const after = Number(minutes);
  const fade = mode === 'fade' ? Number(fadeSeconds) : 0;
  const valid = after > 0 && after <= MAX_MINUTES && fade >= 0 && fade <= MAX_FADE_SECONDS && fade <= after * 60;

  return (
    <Card>
      <CardContent>
        <Typography variant="subtitle2" color="text.secondary" gutterBottom>Sleep timer</Typography>
        {timer ? (
          <Stack direction="row" alignItems="center" justifyContent="space-between" spacing={2}>
            <Typography variant="h6" fontWeight={600}>
              {/* ends is 0 until the bulb's clock is set, so the time left isn't known yet */}
              {timer.ends > 0 ? `Off in ${formatLeft(timer.ends - Math.floor(now / 1000))}` : 'Timer running'}
            </Typography>
            <Button onClick={onCancel} disabled={disabled}>Cancel timer</Button>
          </Stack>
        ) : (
          <Stack spacing={2}>
            <TextField
              label="Turn off after (minutes)"
              type="number"
              value={minutes}
              onChange={(e) => setMinutes(e.target.value)}
              disabled={disabled}
              slotProps={{ htmlInput: { min: 1, max: MAX_MINUTES } }}
            />
            <ToggleButtonGroup
              exclusive
              fullWidth
              size="small"
              value={mode}
              disabled={disabled}
              onChange={(_, v: 'sudden' | 'fade' | null) => { if (v) setMode(v); }}
            >
              <ToggleButton value="sudden">Sudden</ToggleButton>
              <ToggleButton value="fade">Fade out</ToggleButton>
            </ToggleButtonGroup>
            {mode === 'fade' && (
              <TextField
                label="Fade time (seconds)"
                type="number"
                value={fadeSeconds}
                onChange={(e) => setFadeSeconds(e.target.value)}
                disabled={disabled}
                helperText="The light is fully off when the timer ends."
                slotProps={{ htmlInput: { min: 1, max: MAX_FADE_SECONDS } }}
              />
            )}
            <Button
              variant="contained"
              disabled={disabled || !lightOn || !valid}
              onClick={() => onStart(Math.round(after * 60), Math.round(fade))}
            >
              Start timer
            </Button>
            {!lightOn && !disabled && (
              <Typography variant="body2" color="text.secondary">Turn the light on to start a timer.</Typography>
            )}
          </Stack>
        )}
      </CardContent>
    </Card>
  );
}
