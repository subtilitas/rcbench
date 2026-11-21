import { defineConfig } from 'vite';

// The page is served at /rcbench/flash/ on the repository's GitHub Pages site,
// beside the configurator at /rcbench/. `vite preview` serves the build under
// the same base, so the browser tests load the paths the deployed page loads.
export default defineConfig({
  base: '/rcbench/flash/',
  build: {
    outDir: 'dist',
    target: 'es2022',
    assetsInlineLimit: 0,
    modulePreload: false,
  },
  preview: {
    port: 4174,
    strictPort: true,
  },
});
