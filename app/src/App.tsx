import { useEffect, useMemo } from 'react';
import { CssBaseline, ThemeProvider, useMediaQuery } from '@mui/material';
import { HashRouter, Navigate, Route, Routes } from 'react-router-dom';
import { BulbsProvider } from '@/contexts/BulbsContext';
import { SettingsProvider, useSettings } from '@/contexts/SettingsContext';
import BulbListPage from '@/pages/BulbListPage';
import BulbDetailPage from '@/pages/BulbDetailPage';
import SettingsPage from '@/pages/SettingsPage';
import { createAppTheme, themeBackground } from '@/theme/theme';

function Themed() {
  const { settings } = useSettings();
  const systemDark = useMediaQuery('(prefers-color-scheme: dark)');
  const mode = settings.theme === 'system' ? (systemDark ? 'dark' : 'light') : settings.theme;
  const theme = useMemo(() => createAppTheme(mode), [mode]);
  // the browser bar / Android status bar takes the page colour
  useEffect(() => {
    document.querySelector('meta[name="theme-color"]')?.setAttribute('content', themeBackground(mode));
  }, [mode]);
  return (
    <ThemeProvider theme={theme}>
      <CssBaseline />
      <BulbsProvider>
        {/* Hash routes: work from the Android app's file origin and need no server rewrites */}
        <HashRouter>
          <Routes>
            <Route path="/" element={<BulbListPage />} />
            <Route path="/bulb/:id" element={<BulbDetailPage />} />
            <Route path="/settings" element={<SettingsPage />} />
            <Route path="*" element={<Navigate to="/" replace />} />
          </Routes>
        </HashRouter>
      </BulbsProvider>
    </ThemeProvider>
  );
}

export default function App() {
  return (
    <SettingsProvider>
      <Themed />
    </SettingsProvider>
  );
}
