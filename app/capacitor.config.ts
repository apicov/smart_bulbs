import type { CapacitorConfig } from '@capacitor/cli';

const config: CapacitorConfig = {
  appId: 'xyz.apicov.bulbs',
  appName: 'Pico Bulbs',
  webDir: 'dist',
  android: {
    // Android 15 draws apps edge to edge: keep the web view clear of the status and navigation bars
    adjustMarginsForEdgeToEdge: 'auto',
  },
};

export default config;
