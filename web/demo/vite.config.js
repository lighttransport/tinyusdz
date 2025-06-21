import { defineConfig } from 'vite'
//"use strict";
//Object.defineProperty(exports, "__esModule", { value: true });
//var vite_1 = require("vite");
// https://vitejs.dev/config/
//exports.default = (0, vite_1.defineConfig)({
export default defineConfig({
    server: {
        headers: {
            'Cross-Origin-Opener-Policy': 'same-origin',
            'Cross-Origin-Embedder-Policy': 'require-corp',
        },
    },
    build: {
        outDir: '../dist',
    },
    optimizeDeps: {
        exclude: ['tinyusdz'],
    },
});
