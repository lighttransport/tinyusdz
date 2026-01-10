import puppeteer from 'puppeteer';

// WebGPU requires:
// 1. xvfb-run for virtual X display (ANGLE Vulkan needs X11)
// 2. NVIDIA env vars to select real GPU over llvmpipe
// 3. HTTPS origin (data: URLs are not secure contexts)
//
// Run with: xvfb-run -a node test.js

const browser = await puppeteer.launch({
  headless: false,  // Use xvfb instead of headless for GPU
  executablePath: '/usr/bin/google-chrome',
  ignoreDefaultArgs: true,  // Avoid Puppeteer's default --disable-features
  env: {
    ...process.env,
    __NV_PRIME_RENDER_OFFLOAD: '1',
    __GLX_VENDOR_LIBRARY_NAME: 'nvidia',
    __EGL_VENDOR_LIBRARY_FILENAMES: '/usr/share/glvnd/egl_vendor.d/10_nvidia.json',
  },
  args: [
    '--remote-debugging-port=0',
    '--user-data-dir=/tmp/puppeteer-webgpu-test',
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

console.log('Chrome version:', await browser.version());

const page = await browser.newPage();

// WebGPU requires HTTPS origin (not data: URLs)
await page.goto('https://example.com');
await new Promise(r => setTimeout(r, 1000));

const status = await page.evaluate(async () => {
  const result = {
    webgpuAvailable: !!navigator.gpu,
    adapter: null,
    error: null,
  };

  if (!navigator.gpu) {
    result.error = 'navigator.gpu is undefined (requires HTTPS origin)';
    return result;
  }

  try {
    const adapter = await navigator.gpu.requestAdapter();
    if (adapter) {
      result.adapter = {
        vendor: adapter.info?.vendor,
        architecture: adapter.info?.architecture,
        device: adapter.info?.device,
        isFallbackAdapter: adapter.isFallbackAdapter,
      };
    } else {
      result.error = 'No adapter available';
    }
  } catch (e) {
    result.error = e.message;
  }

  return result;
});

console.log('WebGPU Status:', JSON.stringify(status, null, 2));

await browser.close();
