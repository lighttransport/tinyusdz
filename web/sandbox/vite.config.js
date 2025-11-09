import { defineConfig } from 'vite'

// Do not minify(we want to make demo website simple)
export default defineConfig({
    server: {
        headers: {
            'Cross-Origin-Opener-Policy': 'same-origin',
            'Cross-Origin-Embedder-Policy': 'require-corp',
            'Service-Worker-Allowed': '/'
        },
    },
    optimizeDeps: {
        exclude: ['tinyusdz'],
    }
});
