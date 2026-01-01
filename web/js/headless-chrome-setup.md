# Headless Chrome + WebGPU + GPU Acceleration Setup

This document describes how to enable WebGPU in headless Chrome/Puppeteer environments on Linux, with both hardware GPU acceleration and software (SwiftShader) fallback options.

## Quick Start

| Mode | GPU Required | xvfb Required | Performance |
|------|--------------|---------------|-------------|
| Hardware GPU | Yes | Yes | Fast |
| SwiftShader (Software) | No | Yes | Slow but portable |

## Requirements

### For Hardware GPU Acceleration
- Google Chrome 113+ (WebGPU shipped in Chrome 113)
- NVIDIA GPU with driver 525+ (or AMD/Intel with Vulkan support)
- X Virtual Framebuffer (xvfb)
- Puppeteer or similar automation tool

```bash
apt-get install -y xvfb vulkan-tools libnvidia-gl-525
```

### For Software Rendering (SwiftShader)
- Google Chrome 113+
- X Virtual Framebuffer (xvfb)
- No GPU required

```bash
apt-get install -y xvfb
```

## The Problem

Running Chrome with `--headless=new` and typical WebGPU flags fails because:

1. **ANGLE Vulkan requires X11** - Chrome's ANGLE library uses `DisplayVkXcb` which needs an X11 connection
2. **Software fallback** - Without proper configuration, xvfb uses Mesa's llvmpipe (CPU) instead of the GPU
3. **Secure context requirement** - WebGPU only works on HTTPS origins, not `data:` or `file:` URLs

## Solution

### 1. Use xvfb Instead of Headless Mode

```javascript
const browser = await puppeteer.launch({
  headless: false,  // NOT headless - use xvfb instead
  executablePath: '/usr/bin/google-chrome',
  // ...
});
```

Run with:
```bash
xvfb-run -a node your-script.js
```

### 2. Force NVIDIA GPU Selection

Set environment variables to prevent fallback to llvmpipe:

```javascript
const browser = await puppeteer.launch({
  env: {
    ...process.env,
    __NV_PRIME_RENDER_OFFLOAD: '1',
    __GLX_VENDOR_LIBRARY_NAME: 'nvidia',
    __EGL_VENDOR_LIBRARY_FILENAMES: '/usr/share/glvnd/egl_vendor.d/10_nvidia.json',
  },
  // ...
});
```

### 3. Use HTTPS Origins for WebGPU

WebGPU requires a secure context. Use HTTPS URLs:

```javascript
// Won't work - insecure origin
await page.goto('data:text/html,<script>console.log(navigator.gpu)</script>');

// Works - secure origin
await page.goto('https://example.com');
```

For local testing, use `localhost` with a local HTTPS server or navigate to any HTTPS site first.

## Complete Working Example

```javascript
import puppeteer from 'puppeteer';

const browser = await puppeteer.launch({
  headless: false,
  executablePath: '/usr/bin/google-chrome',
  ignoreDefaultArgs: true,
  env: {
    ...process.env,
    __NV_PRIME_RENDER_OFFLOAD: '1',
    __GLX_VENDOR_LIBRARY_NAME: 'nvidia',
    __EGL_VENDOR_LIBRARY_FILENAMES: '/usr/share/glvnd/egl_vendor.d/10_nvidia.json',
  },
  args: [
    '--remote-debugging-port=0',
    '--user-data-dir=/tmp/puppeteer-webgpu',
    '--no-sandbox',
    '--no-first-run',
    '--use-gl=angle',
    '--use-angle=vulkan',
    '--enable-features=Vulkan',
    '--enable-unsafe-webgpu',
    '--disable-gpu-blocklist',
    'about:blank',
  ]
});

const page = await browser.newPage();
await page.goto('https://example.com');

const status = await page.evaluate(async () => {
  if (!navigator.gpu) return { error: 'WebGPU not available' };

  const adapter = await navigator.gpu.requestAdapter();
  return {
    vendor: adapter?.info?.vendor,
    architecture: adapter?.info?.architecture,
  };
});

console.log(status);
// Output: { vendor: 'nvidia', architecture: 'blackwell' }

await browser.close();
```

Run with:
```bash
xvfb-run -a node script.js
```

## Software Rendering with SwiftShader (No GPU)

SwiftShader is a CPU-based Vulkan implementation bundled with Chrome. It provides WebGPU support without requiring a physical GPU, making it ideal for CI/CD pipelines, containers, and cloud environments without GPU access.

### SwiftShader Characteristics

- **Vendor**: `google`
- **Architecture**: `swiftshader`
- **Performance**: 10-100x slower than hardware GPU
- **Compatibility**: Works on any x86_64 Linux system
- **Use cases**: Testing, CI/CD, environments without GPU

### SwiftShader Example

```javascript
import puppeteer from 'puppeteer';

const browser = await puppeteer.launch({
  headless: false,  // Still need xvfb, not true headless
  executablePath: '/usr/bin/google-chrome',
  ignoreDefaultArgs: true,
  args: [
    '--remote-debugging-port=0',
    '--user-data-dir=/tmp/puppeteer-swiftshader',
    '--no-sandbox',
    '--no-first-run',
    '--use-webgpu-adapter=swiftshader',  // Force SwiftShader
    '--enable-unsafe-webgpu',
    '--disable-gpu-blocklist',
    'about:blank',
  ]
});

const page = await browser.newPage();
await page.goto('https://example.com');

const status = await page.evaluate(async () => {
  if (!navigator.gpu) return { error: 'WebGPU not available' };

  const adapter = await navigator.gpu.requestAdapter();
  return {
    vendor: adapter?.info?.vendor,         // "google"
    architecture: adapter?.info?.architecture,  // "swiftshader"
  };
});

console.log(status);
await browser.close();
```

Run with:
```bash
xvfb-run -a node script.js
```

### Why xvfb is Still Required for SwiftShader

Even though SwiftShader is a software renderer, Chrome's WebGPU/Dawn implementation still requires an X11 display connection for initialization. True `--headless=new` mode without xvfb will fail to create a WebGPU adapter.

```bash
# This does NOT work (no adapter):
node script.js  # with headless: 'new'

# This works:
xvfb-run -a node script.js  # with headless: false
```

### SwiftShader Feature Support

SwiftShader supports most WebGPU features but with some limitations:

| Feature | SwiftShader | Hardware GPU |
|---------|-------------|--------------|
| Basic compute shaders | Yes | Yes |
| Texture sampling | Yes | Yes |
| Render pipelines | Yes | Yes |
| Subgroups | Limited | Yes |
| Timestamp queries | No | Yes |
| Performance | Slow | Fast |

### Choosing Between Hardware and SwiftShader

```javascript
// Auto-detect: prefer hardware, fallback to SwiftShader
const adapter = await navigator.gpu.requestAdapter();

if (adapter.info?.architecture === 'swiftshader') {
  console.log('Running on SwiftShader (software)');
  // Adjust workload size for slower performance
} else {
  console.log(`Running on hardware: ${adapter.info?.vendor}`);
}
```

## Chrome Launch Arguments Reference

| Flag | Purpose |
|------|---------|
| `--use-gl=angle` | Use ANGLE for OpenGL |
| `--use-angle=vulkan` | Use Vulkan backend for ANGLE |
| `--enable-features=Vulkan` | Enable Vulkan feature |
| `--enable-unsafe-webgpu` | Enable WebGPU (needed on some configs) |
| `--disable-gpu-blocklist` | Ignore GPU blocklist |
| `--no-sandbox` | Disable sandbox (required for some environments) |
| `--disable-vulkan-surface` | For compute-only WebGPU (no canvas rendering) |
| `--use-webgpu-adapter=swiftshader` | Force SwiftShader software adapter |

## Troubleshooting

### Error: `xcb_connect() failed`

ANGLE Vulkan needs X11. Use `xvfb-run` instead of `--headless=new`.

### WebGL shows "llvmpipe" renderer

The NVIDIA GPU isn't being selected. Verify:
1. Environment variables are set correctly
2. NVIDIA driver is installed: `nvidia-smi`
3. Vulkan works: `vulkaninfo --summary`

### `navigator.gpu` is undefined

1. Check you're on an HTTPS origin (not `data:` or `file:`)
2. Verify Chrome version is 113+
3. Check `chrome://gpu` for WebGPU status

### GPU process crashes

Check logs with:
```javascript
const browser = await puppeteer.launch({
  dumpio: true,
  args: ['--enable-logging=stderr', '--v=1', ...]
});
```

## Verifying GPU Acceleration

```javascript
const gpuInfo = await page.evaluate(() => {
  const canvas = document.createElement('canvas');
  const gl = canvas.getContext('webgl2');
  const ext = gl.getExtension('WEBGL_debug_renderer_info');
  return {
    renderer: gl.getParameter(ext.UNMASKED_RENDERER_WEBGL),
    webgpu: !!navigator.gpu,
  };
});

// Good: "ANGLE (NVIDIA, Vulkan 1.4.x (NVIDIA GeForce RTX ...), NVIDIA)"
// Bad:  "ANGLE (Mesa, llvmpipe ..., OpenGL 4.5)"
```

## Docker / CI Environment Setup

### Dockerfile for SwiftShader (No GPU)

```dockerfile
FROM node:20-slim

# Install Chrome and xvfb
RUN apt-get update && apt-get install -y \
    wget \
    gnupg \
    xvfb \
    && wget -q -O - https://dl.google.com/linux/linux_signing_key.pub | apt-key add - \
    && echo "deb [arch=amd64] http://dl.google.com/linux/chrome/deb/ stable main" >> /etc/apt/sources.list.d/google.list \
    && apt-get update \
    && apt-get install -y google-chrome-stable \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY package*.json ./
RUN npm install
COPY . .

# Run with xvfb
CMD ["xvfb-run", "-a", "node", "script.js"]
```

### GitHub Actions Example

```yaml
jobs:
  webgpu-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Setup Node.js
        uses: actions/setup-node@v4
        with:
          node-version: '20'

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y xvfb google-chrome-stable
          npm install

      - name: Run WebGPU tests
        run: xvfb-run -a npm test
```

### Environment Detection Script

```javascript
async function getWebGPUInfo() {
  if (!navigator.gpu) {
    return { available: false, reason: 'navigator.gpu undefined' };
  }

  const adapter = await navigator.gpu.requestAdapter();
  if (!adapter) {
    return { available: false, reason: 'No adapter' };
  }

  const isSwiftShader = adapter.info?.architecture === 'swiftshader';

  return {
    available: true,
    vendor: adapter.info?.vendor,
    architecture: adapter.info?.architecture,
    isHardware: !isSwiftShader,
    isSoftware: isSwiftShader,
  };
}
```

## References

- [Chrome DevTools Blog: Supercharge Web AI Testing](https://developer.chrome.com/blog/supercharge-web-ai-testing)
- [Chromium Docs: Server-Side Headless Linux with GPUs](https://chromium.googlesource.com/chromium/src/+/main/docs/gpu/server-side-headless-linux-chrome-with-gpus.md)
- [WebGPU Specification](https://www.w3.org/TR/webgpu/)
- [Dawn (WebGPU implementation)](https://dawn.googlesource.com/dawn)
- [SwiftShader](https://github.com/aspect-build/aspect-bazel-lib/blob/main/packages/aspect_rules_swiftshader/swiftshader.bzl)
