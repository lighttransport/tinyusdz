import { defineConfig } from 'vite';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

// Plugin to set correct MIME type for WASM files
function wasmMimePlugin() {
  return {
    name: 'wasm-mime',
    configureServer(server) {
      server.middlewares.use((req, res, next) => {
        if (req.url?.endsWith('.wasm')) {
          res.setHeader('Content-Type', 'application/wasm');
        }
        next();
      });
    }
  };
}

export default defineConfig({
  root: 'viewer',
  base: './',
  plugins: [wasmMimePlugin()],
  resolve: {
    alias: {
      'three': path.resolve(__dirname, 'node_modules/three'),
    }
  },
  server: {
    port: 5173,
    headers: {
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Embedder-Policy': 'require-corp'
    }
  },
  build: {
    outDir: '../dist',
    emptyOutDir: true
  },
  optimizeDeps: {
    exclude: ['tinyusdz']
  },
  assetsInclude: ['**/*.wasm']
});
