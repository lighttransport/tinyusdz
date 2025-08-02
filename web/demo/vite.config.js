import { defineConfig } from 'vite'
import path from 'path'
import { compression } from 'vite-plugin-compression2'
import { viteStaticCopy } from 'vite-plugin-static-copy'

// Do not minify(we want to make demo website simple)
// base: "./" => make asset path relative(required for static hosting of tinyusdz demo page at github pages)
export default defineConfig({
    base: "./",
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
    // Use only 'gzip' for a while('zstd' is avaiable for node > 22.15.0)
    plugins: [
      compression({algorithms: ['gzip']}),
      viteStaticCopy({
        targets: [
          { src: 'node_modules/tinyusdz/tinyusdz.wasm.zst',
            dest: 'assets/'
          },
        ],
      }),
    ],
});
