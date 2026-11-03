import { Box, Button, Card, CardActionArea, Stack, Switch, Typography } from '@mui/material';
import SensorsIcon from '@mui/icons-material/Sensors';
import { Link as RouterLink } from 'react-router-dom';
import { AppShell } from '@/components/AppShell';
import { BulbGlow } from '@/components/BulbGlow';
import { ConnectionBanner } from '@/components/ConnectionBanner';
import { useBulbs } from '@/contexts/BulbsContext';
import { displayName, type Bulb } from '@/core/bulb';

function BulbCard({ bulb }: { bulb: Bulb }) {
  const { send, status } = useBulbs();
  const s = bulb.state;
  const usable = bulb.online && status.kind === 'connected' && !!s;
  return (
    <Card sx={{ opacity: bulb.online ? 1 : 0.55 }}>
      <Stack direction="row" alignItems="center">
        <CardActionArea component={RouterLink} to={`/bulb/${encodeURIComponent(bulb.id)}`} sx={{ p: 2, flex: 1 }}>
          <Stack direction="row" alignItems="center" spacing={2}>
            <BulbGlow state={s} />
            <Box sx={{ minWidth: 0 }}>
              <Typography fontWeight={600} noWrap>{displayName(bulb.id)}</Typography>
              <Typography variant="body2" color="text.secondary" noWrap>
                {!bulb.online ? 'Offline' : !s ? '…' : s.on ? `On · ${Math.round((s.brightness / 255) * 100)}%` : 'Off'}
                {bulb.online && bulb.motion && ' · motion'}
              </Typography>
            </Box>
            {bulb.online && bulb.motion && <SensorsIcon fontSize="small" color="primary" aria-label="Motion" />}
          </Stack>
        </CardActionArea>
        <Switch
          sx={{ mr: 1 }}
          checked={!!s?.on}
          disabled={!usable}
          onChange={(e) => send(bulb.id, { on: e.target.checked })}
          slotProps={{ input: { 'aria-label': `${displayName(bulb.id)} power` } }}
        />
      </Stack>
    </Card>
  );
}

export default function BulbListPage() {
  const { bulbs, status, send } = useBulbs();
  const list = Object.values(bulbs).sort((a, b) => a.id.localeCompare(b.id));
  const anyOn = list.some((b) => b.online && b.state?.on);
  return (
    <AppShell title="Pico Bulbs">
      <ConnectionBanner />
      {list.length > 0 && (
        <Stack direction="row" justifyContent="space-between" alignItems="center">
          <Typography variant="overline" color="text.secondary">{list.length} {list.length === 1 ? 'bulb' : 'bulbs'}</Typography>
          <Button
            size="small"
            disabled={!anyOn || status.kind !== 'connected'}
            onClick={() => list.filter((b) => b.online && b.state?.on).forEach((b) => send(b.id, { on: false, transition: 1 }))}
          >
            All off
          </Button>
        </Stack>
      )}
      {list.map((b) => <BulbCard key={b.id} bulb={b} />)}
      {status.kind === 'connected' && list.length === 0 && (
        <Typography color="text.secondary" sx={{ textAlign: 'center', mt: 6 }}>
          No bulbs yet. A bulb shows up here as soon as it connects to the broker.
        </Typography>
      )}
    </AppShell>
  );
}
