import { useEffect, useState } from 'react';
import {
  Button, Card, CardContent, IconButton, InputAdornment, Stack, TextField, ToggleButton, ToggleButtonGroup, Typography,
} from '@mui/material';
import VisibilityIcon from '@mui/icons-material/Visibility';
import VisibilityOffIcon from '@mui/icons-material/VisibilityOff';
import { useNavigate } from 'react-router-dom';
import { AppShell } from '@/components/AppShell';
import { statusLabel } from '@/core/status';
import { useBulbs } from '@/contexts/BulbsContext';
import { useSettings, type ThemeMode } from '@/contexts/SettingsContext';

export default function SettingsPage() {
  const { settings, loaded, update } = useSettings();
  const { status } = useBulbs();
  const navigate = useNavigate();
  // edited here, applied on Save (each change reconnects)
  const [form, setForm] = useState({ brokerUrl: settings.brokerUrl, username: settings.username, password: settings.password });
  const [showPassword, setShowPassword] = useState(false);
  useEffect(() => {
    if (loaded) setForm({ brokerUrl: settings.brokerUrl, username: settings.username, password: settings.password });
  }, [loaded, settings.brokerUrl, settings.username, settings.password]);

  const urlOk = /^wss?:\/\/[^\s/]+/.test(form.brokerUrl.trim());
  const changed = form.brokerUrl !== settings.brokerUrl || form.username !== settings.username || form.password !== settings.password;

  return (
    <AppShell title="Settings">
      <Card>
        <CardContent>
          <Stack spacing={2} component="form" onSubmit={(e) => {
            e.preventDefault();
            update({ brokerUrl: form.brokerUrl.trim(), username: form.username.trim(), password: form.password });
            navigate('/');
          }}>
            <Typography variant="subtitle1" fontWeight={600}>Broker</Typography>
            <TextField
              label="Broker URL"
              value={form.brokerUrl}
              onChange={(e) => setForm({ ...form, brokerUrl: e.target.value })}
              error={!urlOk}
              helperText={urlOk ? 'MQTT over WebSockets, e.g. wss://mqtt.apicov.xyz:8084' : 'Must start with wss:// (or ws:// on a local network)'}
              inputMode="url"
              autoCapitalize="none"
              autoCorrect="off"
            />
            <TextField
              label="Username"
              value={form.username}
              onChange={(e) => setForm({ ...form, username: e.target.value })}
              autoCapitalize="none"
              autoCorrect="off"
            />
            <TextField
              label="Password"
              type={showPassword ? 'text' : 'password'}
              value={form.password}
              onChange={(e) => setForm({ ...form, password: e.target.value })}
              autoComplete="current-password"
              slotProps={{
                input: {
                  endAdornment: (
                    <InputAdornment position="end">
                      <IconButton aria-label={showPassword ? 'Hide password' : 'Show password'} onClick={() => setShowPassword((v) => !v)} edge="end">
                        {showPassword ? <VisibilityOffIcon /> : <VisibilityIcon />}
                      </IconButton>
                    </InputAdornment>
                  ),
                },
              }}
            />
            <Stack direction="row" alignItems="center" justifyContent="space-between">
              <Typography variant="body2" color="text.secondary">Status: {statusLabel(status.kind)}</Typography>
              <Button type="submit" variant="contained" disabled={!urlOk || !changed}>Save & connect</Button>
            </Stack>
          </Stack>
        </CardContent>
      </Card>

      <Card>
        <CardContent>
          <Typography variant="subtitle1" fontWeight={600} gutterBottom>Appearance</Typography>
          <ToggleButtonGroup
            exclusive
            fullWidth
            value={settings.theme}
            onChange={(_, v: ThemeMode | null) => v && update({ theme: v })}
          >
            <ToggleButton value="system">System</ToggleButton>
            <ToggleButton value="light">Light</ToggleButton>
            <ToggleButton value="dark">Dark</ToggleButton>
          </ToggleButtonGroup>
        </CardContent>
      </Card>
    </AppShell>
  );
}
