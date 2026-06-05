import { defineConfig } from 'vite';
import path from 'path';

// https://vitejs.dev/config/
export default defineConfig({
  // Multi-page app: serve each .html directly and return 404 for unmatched
  // routes instead of falling back to index.html (the default SPA behavior).
  appType: 'mpa',
  server: {
    fs: {
      allow: [
        path.resolve(__dirname),
        '/path/to/mujoco/wasm/dist',
      ],
    },
    headers: {
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Embedder-Policy': 'require-corp',
    },
    watch: {
      ignored: ['**/node_modules/**', '**/assets/**']
    },
  },
  resolve: {
    alias: [
      { find: 'tinyusdz', replacement: path.resolve(__dirname, './src/tinyusdz') },
    ],
  },
  optimizeDeps: {
    exclude: ['tinyusdz'],
  },
});
