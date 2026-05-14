import { defineConfig } from 'vite'
import path from 'path'
import { compression } from 'vite-plugin-compression2'

// Local dev setup:
//   ln -sfn ../../js/src/tinyusdz node_modules/tinyusdz
//
// The alias maps 'tinyusdz/X.js' to the symlinked dir.
// preserveSymlinks: true keeps the node_modules/ path so bare imports
// (three, fzstd) inside tinyusdz source resolve from demo's node_modules.

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
    resolve: {
        alias: [
            { find: 'tinyusdz', replacement: path.resolve(__dirname, 'node_modules/tinyusdz') },
        ],
        preserveSymlinks: true,
    },
    build: {
        rollupOptions: {
          input: {
            main: path.resolve(__dirname, 'index.html'),
            viewer: path.resolve(__dirname, 'viewer.html'),
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
    // Use only gzip here. The local TinyUSDZ source tree contains raw WASM outputs,
    // and Vite will emit those assets from new URL(..., import.meta.url) references.
    plugins: [
      compression({algorithms: ['gzip']}),
    ],
});
