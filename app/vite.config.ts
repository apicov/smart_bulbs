import { defineConfig } from 'vitest/config'
import react from '@vitejs/plugin-react'
import path from 'path'

export default defineConfig({
  plugins: [react()],
  resolve: {
    alias: [{ find: /^@\//, replacement: path.resolve(__dirname, 'src') + '/' }],
  },
  // Syntax down-levelled for older Android WebViews; runtime methods come from core-js (polyfills.ts)
  build: { target: 'es2017' },
  test: {
    environment: 'jsdom',
    globals: true,
    setupFiles: ['src/test/setup.ts'],
  },
})
