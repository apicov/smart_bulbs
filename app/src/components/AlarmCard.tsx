import { useState } from 'react';
import { Alert, Box, Button, Card, CardContent, Chip, MenuItem, Slider, Stack, Switch, TextField, Typography } from '@mui/material';
import { PaletteGrid } from '@/components/PaletteGrid';
import type { Alarm } from '@/core/bulb';

/** Monday first, with the firmware's day numbers (0 = Sunday). */
const DAYS = [
  { label: 'Mon', day: 1 },
  { label: 'Tue', day: 2 },
  { label: 'Wed', day: 3 },
  { label: 'Thu', day: 4 },
  { label: 'Fri', day: 5 },
  { label: 'Sat', day: 6 },
  { label: 'Sun', day: 0 },
];
const RISE_MINUTES = [1, 2, 5, 10, 15, 20, 30, 45, 60];

const two = (n: number) => String(n).padStart(2, '0');
const pct = (b: number) => Math.round((b / 255) * 100);

interface Props {
  alarm: Alarm;
  timeSynced: boolean;
  disabled: boolean;
  onSave: (alarm: Alarm) => void;
}

/** A wake-up alarm, stored and run by the bulb. Edits stay local until saved. */
export function AlarmCard({ alarm, timeSynced, disabled, onSave }: Props) {
  const [draft, setDraft] = useState<Alarm | null>(null);
  const shown = draft ?? alarm;
  const edit = (change: Partial<Alarm>) => setDraft({ ...shown, ...change });

  const riseMinutes = Math.round(shown.rise / 60);
  // The bulb may hold a value the app doesn't offer, set from another client.
  const riseOptions = RISE_MINUTES.includes(riseMinutes) ? RISE_MINUTES : [...RISE_MINUTES, riseMinutes].sort((a, b) => a - b);

  return (
    <Card>
      <CardContent>
        <Stack direction="row" alignItems="center" justifyContent="space-between">
          <Typography variant="subtitle2" color="text.secondary">Wake-up alarm</Typography>
          <Switch
            checked={shown.enabled}
            disabled={disabled}
            onChange={(e) => edit({ enabled: e.target.checked })}
            slotProps={{ input: { 'aria-label': 'Alarm on' } }}
          />
        </Stack>

        {!timeSynced && (
          <Alert severity="warning" sx={{ my: 1 }}>
            This bulb doesn't know the time yet, so the alarm can't run until it gets online.
          </Alert>
        )}

        <Stack spacing={2} sx={{ mt: 1 }}>
          <TextField
            label="Wake-up time"
            type="time"
            value={`${two(shown.hour)}:${two(shown.minute)}`}
            disabled={disabled}
            onChange={(e) => {
              const [hour, minute] = e.target.value.split(':').map(Number);
              if (hour !== undefined && minute !== undefined && !Number.isNaN(hour) && !Number.isNaN(minute)) edit({ hour, minute });
            }}
            helperText="Full brightness at this time."
            slotProps={{ inputLabel: { shrink: true } }}
          />

          <Box>
            <Stack direction="row" flexWrap="wrap" useFlexGap spacing={1}>
              {DAYS.map(({ label, day }) => {
                const selected = shown.days.includes(day);
                return (
                  <Chip
                    key={label}
                    label={label}
                    disabled={disabled}
                    color={selected ? 'primary' : 'default'}
                    variant={selected ? 'filled' : 'outlined'}
                    aria-pressed={selected}
                    onClick={() => edit({ days: selected ? shown.days.filter((d) => d !== day) : [...shown.days, day].sort((a, b) => a - b) })}
                  />
                );
              })}
            </Stack>
            <Typography variant="body2" color="text.secondary" sx={{ mt: 1 }}>
              {shown.days.length === 0 ? 'No days selected: rings once, then turns itself off.' : 'Repeats on the selected days.'}
            </Typography>
          </Box>

          <TextField
            select
            label="Gradually brighten over"
            value={riseMinutes}
            disabled={disabled}
            onChange={(e) => edit({ rise: Number(e.target.value) * 60 })}
          >
            {riseOptions.map((m) => <MenuItem key={m} value={m}>{m} min</MenuItem>)}
          </TextField>

          <Box>
            <Typography variant="body2" color="text.secondary" gutterBottom>Brightness: {pct(shown.brightness)}%</Typography>
            <Slider
              aria-label="Alarm brightness"
              min={1}
              max={255}
              value={shown.brightness}
              disabled={disabled}
              onChange={(_, v) => edit({ brightness: v as number })}
            />
          </Box>

          <Box>
            <Typography variant="body2" color="text.secondary" gutterBottom>Colour</Typography>
            <PaletteGrid current={shown.color} disabled={disabled} onPick={(color) => edit({ color })} />
          </Box>

          <Button
            variant="contained"
            disabled={disabled || draft === null}
            onClick={() => {
              if (draft) onSave(draft);
              setDraft(null);
            }}
          >
            Save alarm
          </Button>
        </Stack>
      </CardContent>
    </Card>
  );
}
