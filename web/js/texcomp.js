// texcomp.js — Basis-free GPU texture compression in the browser.
//
// Pipeline: draw a sample RGBA8 texture -> compress once to the tinyexr `uni`
// UASTC-subset intermediate (WASM) -> detect the browser's compressed-texture
// support -> transcode `uni` to the GPU-native block format the device
// advertises (BC7 desktop / ASTC / ETC2 mobile) -> upload as a
// THREE.CompressedTexture. If no compressed format is available, decode `uni`
// back to RGBA8 and upload a THREE.DataTexture. No basis_universal / KTX2Loader.
//
// This mirrors, in the browser, what tusdview does natively: one transcodable
// asset, per-device GPU format, uncompressed fallback everywhere.

import * as THREE from 'three';
import createTexcompModule from './texcomp/texcomp_web.mjs';

// --- transcode targets (must match texcomp_web.c) ---
const TARGET_BC7 = 0;
const TARGET_ASTC_4x4 = 1;
const TARGET_ETC2_RGBA = 2;
const TARGET_RGBA8 = 3;

// --- GL compressed internalFormats for THREE.CompressedTexture ---
const GL_COMPRESSED_RGBA_BPTC_UNORM = 0x8e8c;      // EXT_texture_compression_bptc
const GL_COMPRESSED_RGBA_ASTC_4x4_KHR = 0x93b0;    // WEBGL_compressed_texture_astc
const GL_COMPRESSED_RGBA8_ETC2_EAC = 0x9278;       // WEBGL_compressed_texture_etc

const els = {};
function log(msg) {
  const p = document.getElementById('log');
  if (p) p.textContent += msg + '\n';
  console.log('[texcomp]', msg);
}
function setStat(id, v) {
  const e = document.getElementById(id);
  if (e) e.textContent = v;
}

// Detect the browser's compressed-texture support and pick a transcode target.
function detectTarget(gl) {
  const bptc = gl.getExtension('EXT_texture_compression_bptc');
  const astc = gl.getExtension('WEBGL_compressed_texture_astc');
  const etc = gl.getExtension('WEBGL_compressed_texture_etc');
  const s3tc = gl.getExtension('WEBGL_compressed_texture_s3tc');
  const support = [];
  if (s3tc) support.push('s3tc(BC1/3)');
  if (bptc) support.push('bptc(BC6H/BC7)');
  if (astc) support.push('astc');
  if (etc) support.push('etc2');
  setStat('caps', support.length ? support.join(', ') : 'none (RGBA fallback)');

  // Prefer the highest-quality format the device supports.
  if (bptc) return { target: TARGET_BC7, glFormat: GL_COMPRESSED_RGBA_BPTC_UNORM, name: 'BC7' };
  if (astc) return { target: TARGET_ASTC_4x4, glFormat: GL_COMPRESSED_RGBA_ASTC_4x4_KHR, name: 'ASTC 4x4' };
  if (etc) return { target: TARGET_ETC2_RGBA, glFormat: GL_COMPRESSED_RGBA8_ETC2_EAC, name: 'ETC2 RGBA' };
  return { target: TARGET_RGBA8, glFormat: 0, name: 'RGBA8 (uncompressed fallback)' };
}

// Draw a detailed, compressible sample texture into an RGBA8 Uint8Array.
function makeSampleRGBA(size) {
  const c = document.createElement('canvas');
  c.width = c.height = size;
  const g = c.getContext('2d');
  const grad = g.createLinearGradient(0, 0, size, size);
  grad.addColorStop(0, '#1e3a8a');
  grad.addColorStop(0.5, '#db2777');
  grad.addColorStop(1, '#f59e0b');
  g.fillStyle = grad;
  g.fillRect(0, 0, size, size);
  // some geometry + text so the compressor has real detail to work with
  g.strokeStyle = 'rgba(255,255,255,0.5)';
  g.lineWidth = 2;
  for (let i = 0; i < size; i += size / 16) {
    g.beginPath(); g.moveTo(i, 0); g.lineTo(i, size); g.stroke();
    g.beginPath(); g.moveTo(0, i); g.lineTo(size, i); g.stroke();
  }
  g.fillStyle = '#fff';
  g.font = `bold ${Math.floor(size / 6)}px sans-serif`;
  g.textAlign = 'center';
  g.fillText('KTX2', size / 2, size / 2);
  g.font = `${Math.floor(size / 12)}px sans-serif`;
  g.fillText('uni transcode', size / 2, size * 0.68);
  const img = g.getImageData(0, 0, size, size);
  return new Uint8Array(img.data.buffer);
}

// Compress rgba -> uni -> transcode to `pick.target`. Returns {texture, bytes}.
function buildTexture(M, rgba, w, h, pick) {
  const free = [];
  const alloc = (n) => { const p = M._malloc(n); free.push(p); return p; };
  try {
    const rgbaPtr = alloc(rgba.length);
    M.HEAPU8.set(rgba, rgbaPtr);

    const uniSize = M._tcw_uni_size(w, h);
    const uniPtr = alloc(uniSize);
    if (M._tcw_compress_uni(rgbaPtr, w, h, uniPtr, uniSize) !== 0)
      throw new Error('uni compress failed');
    setStat('uni', `${(uniSize / 1024).toFixed(1)} KiB`);

    if (pick.target === TARGET_RGBA8) {
      // Universal fallback: decode uni -> RGBA8, upload a DataTexture.
      const outPtr = alloc(w * h * 4);
      if (M._tcw_decompress_rgba8(uniPtr, w, h, outPtr, w * h * 4) !== 0)
        throw new Error('uni decode failed');
      const data = M.HEAPU8.slice(outPtr, outPtr + w * h * 4);
      const tex = new THREE.DataTexture(data, w, h, THREE.RGBAFormat);
      tex.needsUpdate = true;
      return { texture: tex, bytes: data.length };
    }

    const blockBytes = M._tcw_block_size(w, h);
    const blkPtr = alloc(blockBytes);
    if (M._tcw_transcode(uniPtr, w, h, pick.target, blkPtr, blockBytes) !== 0)
      throw new Error('transcode failed');
    const blocks = M.HEAPU8.slice(blkPtr, blkPtr + blockBytes);
    const tex = new THREE.CompressedTexture(
      [{ data: blocks, width: w, height: h }], w, h, pick.glFormat,
      THREE.UnsignedByteType);
    tex.needsUpdate = true;
    return { texture: tex, bytes: blockBytes };
  } finally {
    for (const p of free) M._free(p);
  }
}

async function main() {
  const canvas = document.getElementById('view');
  const renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
  renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
  const resize = () => {
    const w = canvas.clientWidth, h = canvas.clientHeight;
    renderer.setSize(w, h, false);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
  };
  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0x111318);
  const camera = new THREE.PerspectiveCamera(45, 1, 0.1, 100);
  camera.position.set(0, 0, 3);
  scene.add(new THREE.AmbientLight(0xffffff, 1.0));

  const gl = renderer.getContext();
  const isWebGL2 = renderer.capabilities.isWebGL2;
  setStat('backend', isWebGL2 ? 'WebGL2' : 'WebGL1');
  const pick = detectTarget(gl);
  setStat('format', pick.name);
  log(`chosen GPU format: ${pick.name}`);

  log('loading texcomp WASM…');
  const M = await createTexcompModule();
  log('WASM ready.');

  const size = 256;
  const rgba = makeSampleRGBA(size);
  const rgbaBytes = rgba.length;
  setStat('rgba', `${(rgbaBytes / 1024).toFixed(1)} KiB`);

  let result;
  try {
    result = buildTexture(M, rgba, size, size, pick);
  } catch (e) {
    log('compressed path failed (' + e.message + '); using RGBA fallback.');
    result = buildTexture(M, rgba, size, size,
      { target: TARGET_RGBA8, glFormat: 0, name: 'RGBA8 fallback' });
    setStat('format', 'RGBA8 (fallback after error)');
  }
  const tex = result.texture;
  tex.colorSpace = THREE.SRGBColorSpace;

  setStat('gpu', `${(result.bytes / 1024).toFixed(1)} KiB`);
  const ratio = rgbaBytes / result.bytes;
  setStat('savings', `${ratio.toFixed(1)}x smaller than RGBA8`);
  log(`GPU upload: ${(result.bytes / 1024).toFixed(1)} KiB (${ratio.toFixed(1)}x vs ${(rgbaBytes / 1024).toFixed(1)} KiB RGBA8)`);

  const mesh = new THREE.Mesh(
    new THREE.BoxGeometry(1.4, 1.4, 1.4),
    new THREE.MeshBasicMaterial({ map: tex }));
  scene.add(mesh);

  window.addEventListener('resize', resize);
  resize();
  renderer.setAnimationLoop((t) => {
    mesh.rotation.y = t * 0.0005;
    mesh.rotation.x = t * 0.0003;
    renderer.render(scene, camera);
  });
}

main().catch((e) => { log('ERROR: ' + e.message); console.error(e); });
