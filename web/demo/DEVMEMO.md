# Development Memo: Local TinyUSDZ Module Integration with Vite

## Overview
This document describes how to configure the Vite development environment to use the local TinyUSDZ module from `../js/src/tinyusdz/` instead of the npm package, enabling live reload for rapid development.

## Directory Structure
```
web/
├── demo/                    # Current directory (Vite project)
│   ├── package.json
│   ├── vite.config.js
│   └── ...
└── js/
    └── src/
        └── tinyusdz/       # Local TinyUSDZ module source
            ├── TinyUSDZLoader.js
            ├── TinyUSDZComposer.js
            ├── TinyUSDZLoaderUtils.js
            └── TinyUSDZMCPClient.js
```

## Configuration Steps

### Step 1: Create package.json for Local TinyUSDZ Module

Create `web/js/src/tinyusdz/package.json`:

```json
{
  "name": "tinyusdz",
  "version": "0.0.1",
  "type": "module",
  "main": "./TinyUSDZLoader.js",
  "exports": {
    ".": "./TinyUSDZLoader.js",
    "./TinyUSDZLoader": "./TinyUSDZLoader.js",
    "./TinyUSDZComposer": "./TinyUSDZComposer.js",
    "./TinyUSDZLoaderUtils": "./TinyUSDZLoaderUtils.js",
    "./TinyUSDZMCPClient": "./TinyUSDZMCPClient.js"
  }
}
```

### Step 2: Update demo/package.json

Replace the npm package with a local file reference:

```json
{
  "dependencies": {
    // Remove this line:
    // "tinyusdz": "0.9.5-rc.7",

    // Add this line:
    "tinyusdz": "file:../js/src/tinyusdz",

    // ... other dependencies
  }
}
```

### Step 3: Update vite.config.js

Add path resolution and watch configuration:

```javascript
import { defineConfig } from 'vite'
import path from 'path'
import { compression } from 'vite-plugin-compression2'
import { viteStaticCopy } from 'vite-plugin-static-copy'

export default defineConfig({
    base: "./",
    server: {
        headers: {
            'Cross-Origin-Opener-Policy': 'same-origin',
            'Cross-Origin-Embedder-Policy': 'require-corp',
        },
        // Enable watching for local module changes
        watch: {
            // Don't ignore the local TinyUSDZ module
            ignored: ['!**/web/js/src/tinyusdz/**']
        }
    },
    resolve: {
        alias: {
            // Direct alias to local module (optional but recommended)
            'tinyusdz': path.resolve(__dirname, '../js/src/tinyusdz')
        }
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
        terserOptions: false,
    },
    optimizeDeps: {
        // Exclude from pre-bundling for better HMR
        exclude: ['tinyusdz'],
    },
    plugins: [
        compression({algorithms: ['gzip']}),
        viteStaticCopy({
            targets: [
                // Update path if WASM file is in local module
                {
                    src: '../js/src/tinyusdz/tinyusdz.wasm.zst',
                    dest: 'assets/'
                },
            ],
        }),
    ],
});
```

### Step 4: Install Dependencies

After making the above changes:

```bash
cd web/demo
npm install  # This will create a symlink to the local module
```

### Alternative: Using npm link (Symlink Method)

Instead of the file reference, you can use npm link for a cleaner setup:

```bash
# First, register the local module
cd web/js/src/tinyusdz
npm link

# Then link it in the demo project
cd web/demo
npm link tinyusdz
```

## Usage in Code

After configuration, import TinyUSDZ modules as normal:

```javascript
// Default export
import TinyUSDZLoader from 'tinyusdz';

// Named exports
import { TinyUSDZLoader } from 'tinyusdz';

// Specific module imports
import TinyUSDZComposer from 'tinyusdz/TinyUSDZComposer';
import TinyUSDZLoaderUtils from 'tinyusdz/TinyUSDZLoaderUtils';
import TinyUSDZMCPClient from 'tinyusdz/TinyUSDZMCPClient';
```

## Development Workflow

1. **Start the dev server:**
   ```bash
   npm run dev
   ```

2. **Edit files in `web/js/src/tinyusdz/`**
   - Changes will trigger automatic HMR (Hot Module Replacement)
   - The browser will reload with your changes instantly

3. **Benefits:**
   - ✅ No need to rebuild/republish npm packages
   - ✅ Instant feedback on code changes
   - ✅ Direct debugging of source code
   - ✅ Simplified development workflow

## Troubleshooting

### Issue: Changes not triggering reload
- Check that the watch configuration in vite.config.js is correct
- Ensure the path in the alias points to the correct directory
- Try restarting the Vite dev server

### Issue: Module not found errors
- Verify the package.json exists in `web/js/src/tinyusdz/`
- Check that the exports field correctly maps to existing files
- Run `npm install` again after configuration changes

### Issue: WASM file not loading
- Update the vite-plugin-static-copy path to match your WASM file location
- Ensure the WASM file exists at the specified path

## Notes

- The `optimizeDeps.exclude` setting prevents Vite from pre-bundling the local module, ensuring HMR works correctly
- The watch configuration tells Vite to monitor the local module directory for changes
- Using `type: "module"` in both package.json files ensures ES module compatibility
- The file reference (`file:../js/src/tinyusdz`) creates a symlink in node_modules, treating the local folder as a package

## Production Build

For production builds, you may want to switch back to the published npm package:
1. Update package.json to use the npm version
2. Run `npm install`
3. Build with `npm run build`

---
*Last updated: 2025-10-27*