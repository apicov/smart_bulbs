import './polyfills';
import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { Capacitor } from '@capacitor/core';
import { App as NativeApp } from '@capacitor/app';
import App from './App';

if (Capacitor.isNativePlatform()) {
  // Android back button: back to the bulb list, leave the app from the list
  void NativeApp.addListener('backButton', () => {
    if (window.location.hash !== '#/' && window.location.hash !== '') window.location.hash = '#/';
    else void NativeApp.exitApp();
  });
}

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
