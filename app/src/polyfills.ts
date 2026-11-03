/**
 * Polyfills — must be imported before anything else in main.tsx.
 *
 * The app ships inside the Android System WebView (and iOS WKWebView), which on older devices can be
 * several Chromium/Safari versions behind. esbuild/Vite only down-level *syntax*, never runtime
 * methods, so modern stdlib methods (`Array.prototype.at`, `findLast`, `Object.hasOwn`, `replaceAll`,
 * …) and newer Web APIs would otherwise throw at runtime. Copied from the_stitch_tracker, where that
 * broke saves on an old test tablet. See the build `target` + `browserslist` in vite.config.ts / package.json.
 *
 * `core-js/stable` covers the full ECMAScript stdlib for our browserslist floor. Web APIs that live
 * outside ECMAScript (and so aren't in core-js) are polyfilled explicitly below.
 */

import 'core-js/stable';

// crypto.randomUUID(): Chromium 92 / Safari 15.4. Not part of core-js (it's a Web Crypto API).
if (typeof crypto !== 'undefined' && typeof crypto.randomUUID !== 'function') {
  (crypto as Crypto & { randomUUID: () => `${string}-${string}-${string}-${string}-${string}` }).randomUUID =
    function randomUUID() {
      // RFC 4122 v4 using crypto.getRandomValues when available, else Math.random.
      const bytes = new Uint8Array(16);
      if (typeof crypto.getRandomValues === 'function') {
        crypto.getRandomValues(bytes);
      } else {
        for (let i = 0; i < 16; i++) bytes[i] = Math.floor(Math.random() * 256);
      }
      bytes[6] = (bytes[6] & 0x0f) | 0x40; // version 4
      bytes[8] = (bytes[8] & 0x3f) | 0x80; // variant 10
      const hex = Array.from(bytes, (b) => b.toString(16).padStart(2, '0'));
      return `${hex[0]}${hex[1]}${hex[2]}${hex[3]}-${hex[4]}${hex[5]}-${hex[6]}${hex[7]}-${hex[8]}${hex[9]}-${hex[10]}${hex[11]}${hex[12]}${hex[13]}${hex[14]}${hex[15]}` as `${string}-${string}-${string}-${string}-${string}`;
    };
}
