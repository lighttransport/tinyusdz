import { defineConfig } from 'vite'
import path from 'path'

// Do not minify(we want to make demo website simple)
export default defineConfig({
    server: {
        headers: {
            'Cross-Origin-Opener-Policy': 'same-origin',
            'Cross-Origin-Embedder-Policy': 'require-corp',
        },
    },
    build: {
        rollupOptions: {
          input: {
            main: path.resolve(__dirname, 'index.html'),
            demos: path.resolve(__dirname, 'demos.html'),
            basic_usd_composite: path.resolve(__dirname, 'basic-usd-composite.html'),
            usda_load: path.resolve(__dirname, 'usda-load.html'),
          },
        },
        minify: false,
        terserOptions: false, // Disable terser completely
    },
    optimizeDeps: {
        exclude: ['tinyusdz'],
    },
});
