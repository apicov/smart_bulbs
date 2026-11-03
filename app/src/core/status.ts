import type { ConnectionStatus } from '@/contexts/BulbsContext';

export function statusLabel(kind: ConnectionStatus['kind']): string {
  return { connected: 'Connected', connecting: 'Connecting…', offline: 'Offline', denied: 'Login refused', unconfigured: 'Not set up' }[kind];
}
