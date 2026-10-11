import { defineConfig } from 'vite';

// The page is a GitHub Pages project site, served at /rcbench/. `vite preview`
// serves the build under the same base, so the smoke test loads the paths the
// deployed page loads.
export default defineConfig({
  base: '/rcbench/',
  build: {
    outDir: 'dist',
    target: 'es2022',
    assetsInlineLimit: 0,
  },
  preview: {
    port: 4173,
    strictPort: true,
  },
});
