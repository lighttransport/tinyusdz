import { defineConfig } from 'vite';
import path from 'path';

// https://vitejs.dev/config/
export default defineConfig({
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
