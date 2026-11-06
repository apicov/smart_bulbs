import { useEffect, useRef, useState } from 'react';
import { Alert, Box, Card, CardContent, Collapse, Button, Slider, Stack, Switch, Typography } from '@mui/material';
import BrightnessLowIcon from '@mui/icons-material/BrightnessLow';
import BrightnessHighIcon from '@mui/icons-material/BrightnessHigh';
import PaletteIcon from '@mui/icons-material/PaletteOutlined';
import { RgbColorPicker } from 'react-colorful';
import { useParams } from 'react-router-dom';
import { AlarmCard } from '@/components/AlarmCard';
import { AppShell } from '@/components/AppShell';
import { BulbGlow } from '@/components/BulbGlow';
import { ConnectionBanner } from '@/components/ConnectionBanner';
import { PaletteGrid } from '@/components/PaletteGrid';
import { SleepTimerCard } from '@/components/SleepTimerCard';
import { useBulbs } from '@/contexts/BulbsContext';
import { useThrottled } from '@/hooks/useThrottled';
import { displayName, type BulbCommand, type Rgb } from '@/core/bulb';

/** While dragging: short fades so the lamp follows the finger; values sent ~12×/s at QoS 0. */
const DRAG_TRANSITION = 0.15;
const DRAG_INTERVAL_MS = 80;
/** The colour picker has no "drag ended" event: its last value is sent this long after the last move. */
const PICKER_SETTLE_MS = 350;

const pct = (b: number) => Math.round((b / 255) * 100);

export default function BulbDetailPage() {
  const { id = '' } = useParams();
  const { bulbs, status, send, sendSchedule } = useBulbs();
  const bulb = bulbs[id];
  const s = bulb?.state ?? null;
  const usable = !!bulb?.online && status.kind === 'connected' && !!s;

  // While a control is being dragged it shows its own value, not the bulb's (late replies would make it jump)
  const [draftBrightness, setDraftBrightness] = useState<number | null>(null);
  const [draftColor, setDraftColor] = useState<Rgb | null>(null);
  const [showPicker, setShowPicker] = useState(false);
  const settle = useRef<ReturnType<typeof setTimeout> | null>(null);
  useEffect(() => () => { if (settle.current) clearTimeout(settle.current); }, []);

  const live = useThrottled((cmd: BulbCommand) => send(id, cmd, 0), DRAG_INTERVAL_MS);
  const final = (cmd: BulbCommand) => { live.cancel(); send(id, cmd, 1); };

  const brightness = draftBrightness ?? s?.brightness ?? 128;
  const color = draftColor ?? s?.color ?? { r: 255, g: 180, b: 100 };
  const shown = s ? { ...s, brightness, color } : null;

  return (
    <AppShell title={displayName(id)}>
      <ConnectionBanner />
      {!bulb && status.kind === 'connected' && <Alert severity="info">This bulb hasn't reported in.</Alert>}
      {bulb && !bulb.online && <Alert severity="warning">This bulb is offline. It shows its last known state.</Alert>}

      <Card>
        <CardContent>
          <Stack direction="row" alignItems="center" spacing={2}>
            <BulbGlow state={shown} size={64} />
            <Box sx={{ flex: 1 }}>
              <Typography variant="h6" fontWeight={600}>{s?.on ? `${pct(brightness)}%` : 'Off'}</Typography>
              <Typography variant="body2" color="text.secondary">
                {bulb?.motion ? 'Motion detected' : 'No motion'}
              </Typography>
            </Box>
            <Switch
              checked={!!s?.on}
              disabled={!usable}
              onChange={(e) => send(id, { on: e.target.checked })}
              slotProps={{ input: { 'aria-label': 'Power' } }}
            />
          </Stack>

          <Stack direction="row" alignItems="center" spacing={2} sx={{ mt: 3 }}>
            <BrightnessLowIcon color="action" />
            <Slider
              aria-label="Brightness"
              min={1}
              max={255}
              value={brightness}
              disabled={!usable}
              valueLabelDisplay="auto"
              valueLabelFormat={(v) => `${pct(v)}%`}
              onChange={(_, v) => {
                const b = v as number;
                setDraftBrightness(b);
                live.call({ on: true, brightness: b, transition: DRAG_TRANSITION });
              }}
              onChangeCommitted={(_, v) => {
                final({ on: true, brightness: v as number, transition: DRAG_TRANSITION });
                setDraftBrightness(null);
              }}
            />
            <BrightnessHighIcon color="action" />
          </Stack>
        </CardContent>
      </Card>

      <Card>
        <CardContent>
          <Typography variant="subtitle2" color="text.secondary" gutterBottom>Colour</Typography>
          <PaletteGrid current={s?.color} disabled={!usable} onPick={(c) => send(id, { on: true, color: c })} />
          <Button
            startIcon={<PaletteIcon />}
            onClick={() => setShowPicker((v) => !v)}
            disabled={!usable}
            sx={{ mt: 2 }}
          >
            {showPicker ? 'Hide custom colour' : 'Custom colour'}
          </Button>
          <Collapse in={showPicker && usable} unmountOnExit>
            <Box sx={{ mt: 2, '& .react-colorful': { width: '100%', height: 220 } }}>
              <RgbColorPicker
                color={color}
                onChange={(c) => {
                  setDraftColor(c);
                  live.call({ on: true, color: c, transition: DRAG_TRANSITION });
                  if (settle.current) clearTimeout(settle.current);
                  settle.current = setTimeout(() => {
                    final({ on: true, color: c, transition: DRAG_TRANSITION });
                    setDraftColor(null);
                  }, PICKER_SETTLE_MS);
                }}
              />
            </Box>
          </Collapse>
        </CardContent>
      </Card>

      {bulb?.schedule && (
        <>
          <SleepTimerCard
            timer={bulb.schedule.timer}
            lightOn={!!s?.on}
            disabled={!usable}
            onStart={(after, fade) => sendSchedule(id, { timer: { after, fade } })}
            onCancel={() => sendSchedule(id, { timer: null })}
          />
          <AlarmCard
            alarm={bulb.schedule.alarm}
            timeSynced={bulb.schedule.timeSynced}
            disabled={!usable}
            onSave={(alarm) => sendSchedule(id, { alarm })}
          />
        </>
      )}
    </AppShell>
  );
}
