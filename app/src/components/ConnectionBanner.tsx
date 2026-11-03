import { Alert, Button } from '@mui/material';
import { Link as RouterLink } from 'react-router-dom';
import { useBulbs } from '@/contexts/BulbsContext';

/** Says why bulbs can't be controlled right now (nothing when connected). */
export function ConnectionBanner() {
  const { status } = useBulbs();
  const toSettings = <Button component={RouterLink} to="/settings" color="inherit" size="small">Settings</Button>;
  switch (status.kind) {
    case 'connected':
    case 'connecting':
      return null;
    case 'unconfigured':
      return <Alert severity="info" action={toSettings}>Enter the broker password to find your bulbs.</Alert>;
    case 'denied':
      return <Alert severity="error" action={toSettings}>{status.message}</Alert>;
    case 'offline':
      return <Alert severity="warning">{status.message}</Alert>;
  }
}
