import { defineConfig } from 'vite'
import path from 'path'
import { compression } from 'vite-plugin-compression2'

// The demo intentionally resolves TinyUSDZ from the npm package in
// node_modules instead of the repository's web/js source tree.

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
    },
    build: {
        rollupOptions: {
          input: {
            main: path.resolve(__dirname, 'index.html'),
            materialx_node: path.resolve(__dirname, 'materialx-node.html'),
            materialx_physical: path.resolve(__dirname, 'materialx-physical.html'),
            usdlux: path.resolve(__dirname, 'usdlux.html'),
            skinning: path.resolve(__dirname, 'skinning.html'),
            xform_skinning_animation: path.resolve(__dirname, 'xform-skinning-animation.html'),
            physics: path.resolve(__dirname, 'physics.html'),
            asset_resolver: path.resolve(__dirname, 'asset-resolver.html'),
            composition: path.resolve(__dirname, 'composition.html'),
            export_demo: path.resolve(__dirname, 'export.html'),
          },
        },
        minify: false,
        terserOptions: false, // Disable terser completely
    },
    optimizeDeps: {
        exclude: ['tinyusdz'],
    },
    // Use only gzip here. Vite will emit the WASM referenced by the npm package.
    plugins: [
      compression({algorithms: ['gzip']}),
    ],
});
