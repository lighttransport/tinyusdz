import { defineConfig } from 'vite';
import path from 'path';

// https://vitejs.dev/config/
export default defineConfig({
  server: {
    headers: {
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Embedder-Policy': 'require-corp',
    },
  },
  resolve: {
    alias: [
      { find: 'tinyusdz', replacement: path.resolve(__dirname, '/src/tinyusdz') },
    ],
  },
  optimizeDeps: {
    exclude: ['tinyusdz'],
  },
});

