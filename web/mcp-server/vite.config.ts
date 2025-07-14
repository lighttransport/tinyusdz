import { defineConfig } from 'vite'
import path from 'path'

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
    optimizeDeps: {
        exclude: ['tinyusdz'],
    },
});
