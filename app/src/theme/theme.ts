import { createTheme, type Theme } from '@mui/material';
import '@fontsource/inter/400.css';
import '@fontsource/inter/500.css';
import '@fontsource/inter/600.css';

export const UI_FONT = '"Inter", "Noto Sans", system-ui, sans-serif';

const COLORS = {
  light: { background: '#f6f5f2', surface: '#ffffff', text: '#222222', muted: '#6b675f', accent: '#c77d16', divider: '#e4e0d8' },
  dark: { background: '#121212', surface: '#1c1c1c', text: '#e6e3dd', muted: '#9a968e', accent: '#f0a33a', divider: '#2c2b29' },
} as const;

export const themeBackground = (mode: 'light' | 'dark') => COLORS[mode].background;

/** Warm amber accent (it's a lamp app); quiet surfaces so the bulbs' own colours stand out. */
export function createAppTheme(mode: 'light' | 'dark'): Theme {
  const c = COLORS[mode];
  return createTheme({
    palette: {
      mode,
      primary: { main: c.accent },
      background: { default: c.background, paper: c.surface },
      text: { primary: c.text, secondary: c.muted },
      divider: c.divider,
    },
    typography: { fontFamily: UI_FONT, button: { textTransform: 'none', fontWeight: 500 } },
    shape: { borderRadius: 12 },
    components: {
      MuiAppBar: {
        defaultProps: { elevation: 0, color: 'inherit' },
        styleOverrides: { root: { backgroundColor: c.background, borderBottom: `1px solid ${c.divider}` } },
      },
      MuiPaper: { styleOverrides: { root: { backgroundImage: 'none' } } },
      MuiCard: { defaultProps: { variant: 'outlined' } },
    },
  });
}
