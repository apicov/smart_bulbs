import type { ReactNode } from 'react';
import { AppBar, Box, IconButton, Toolbar, Tooltip, Typography } from '@mui/material';
import ArrowBackIcon from '@mui/icons-material/ArrowBack';
import SettingsIcon from '@mui/icons-material/SettingsOutlined';
import { Link as RouterLink, useLocation, useNavigate } from 'react-router-dom';
import { useBulbs } from '@/contexts/BulbsContext';
import { statusLabel } from '@/core/status';

const DOT = { connected: 'success.main', connecting: 'warning.main', offline: 'warning.main', denied: 'error.main', unconfigured: 'text.disabled' } as const;

/** Top bar (back, title, connection dot, settings) around every page. */
export function AppShell({ title, children }: { title: string; children: ReactNode }) {
  const { status } = useBulbs();
  const { pathname } = useLocation();
  const navigate = useNavigate();
  const isHome = pathname === '/';
  return (
    <Box sx={{ minHeight: '100vh', bgcolor: 'background.default' }}>
      <AppBar position="sticky">
        <Toolbar sx={{ gap: 1 }}>
          {!isHome && (
            <IconButton edge="start" aria-label="Back" onClick={() => navigate('/')}>
              <ArrowBackIcon />
            </IconButton>
          )}
          <Typography variant="h6" sx={{ flex: 1, fontWeight: 600 }} noWrap>{title}</Typography>
          <Tooltip title={statusLabel(status.kind)}>
            <Box aria-label={statusLabel(status.kind)} sx={{ width: 10, height: 10, borderRadius: '50%', bgcolor: DOT[status.kind] }} />
          </Tooltip>
          {pathname !== '/settings' && (
            <IconButton aria-label="Settings" component={RouterLink} to="/settings" edge="end">
              <SettingsIcon />
            </IconButton>
          )}
        </Toolbar>
      </AppBar>
      <Box component="main" sx={{ maxWidth: 640, mx: 'auto', p: 2, display: 'flex', flexDirection: 'column', gap: 2 }}>
        {children}
      </Box>
    </Box>
  );
}
