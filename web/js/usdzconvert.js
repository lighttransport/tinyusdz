// usdzconvert — TinyUSDZ WASM
// Upload a folder or multiple files (USD + textures), convert to a USDZ with
// options (resize / re-encode textures), and download the result.
// Also includes a standalone texture channel-repack tool.
//
// NOTE: fpnge uses x86 SIMD and is not compiled for WASM, so PNG re-encoding
// here uses the portable `fpng` encoder. The native `tusdzconvert` CLI uses
// fpnge.

import {
  loadWasm,
  isImageName,
  convertFolderToUSDZ,
  parseByteSize,
} from './src/usdzconvert.js';

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

const container = document.createElement('div');
container.style.cssText = 'max-width:820px;margin:32px auto;padding:20px;font-family:system-ui,sans-serif;color:#eee';
container.innerHTML = `
  <h1 style="margin:0 0 6px">TinyUSDZ — usdzconvert</h1>
  <p style="color:#aaa;margin:0 0 18px">
    Drop or pick a <b>folder</b> (USD + textures) or multiple files, set options, convert to USDZ, and download.
  </p>

  <div id="drop" style="border:2px dashed #555;border-radius:8px;padding:22px;text-align:center;margin-bottom:14px;background:#1a1a2e">
    <div style="margin-bottom:10px;color:#bbb">Drag &amp; drop a folder or files here, or:</div>
    <label class="btn">Choose Folder<input id="folderInput" type="file" webkitdirectory multiple style="display:none"></label>
    <label class="btn">Choose Files<input id="filesInput" type="file" multiple
       accept=".usd,.usda,.usdc,.usdz,.png,.jpg,.jpeg,.exr" style="display:none"></label>
  </div>

  <div id="fileList" style="display:none;background:#111;border-radius:6px;padding:10px;margin-bottom:14px;font-size:13px;max-height:160px;overflow:auto"></div>

  <fieldset style="border:1px solid #444;border-radius:6px;padding:12px;margin-bottom:14px">
    <legend style="color:#bbb">Options</legend>
    <div style="display:grid;grid-template-columns:auto 1fr;gap:8px 12px;align-items:center;font-size:14px">
      <label>Root USD file</label>
      <select id="rootSelect" style="padding:4px"></select>

      <label>Max texture size (px)</label>
      <input id="maxSize" type="number" min="0" step="64" value="0" style="padding:4px;width:120px" title="0 = do not resize">

      <label>Target total texture size</label>
      <input id="targetSize" type="text" placeholder="e.g. 100MB (blank = off)" style="padding:4px;width:200px"
             title="Shrink all textures so their total fits this size">

      <label>Fit strategy</label>
      <div style="display:flex;gap:14px;align-items:center">
        <label style="display:inline-flex;gap:4px"><input type="radio" name="fitStrategy" value="size" checked> Texture size</label>
        <label style="display:inline-flex;gap:4px"><input type="radio" name="fitStrategy" value="quality"> JPEG quality</label>
      </div>

      <label>Re-encode textures</label>
      <input id="reencode" type="checkbox" checked style="justify-self:start">

      <label>JPEG quality</label>
      <input id="jpegQuality" type="number" min="1" max="100" value="90" style="padding:4px;width:120px">
    </div>
    <p style="color:#888;font-size:12px;margin:10px 0 0">
      Set a <b>target total texture size</b> to auto-fit all textures to a budget — choose the lever:
      reduce <b>texture size</b> (keeps PNG) or lower <b>JPEG quality</b> (transcodes to JPG).
      Without a target, textures are resized/re-encoded in place. PNG uses the portable <code>fpng</code> encoder in WASM.
    </p>
  </fieldset>

  <div style="display:flex;gap:8px;align-items:center;margin-bottom:8px">
    <button id="btnConvert" class="btn primary" disabled>Convert &amp; Download USDZ</button>
    <span id="status" style="color:#aaa;font-size:13px"></span>
  </div>

  <details style="margin-top:18px">
    <summary style="cursor:pointer;color:#bbb">Texture repack tool (merge channels, e.g. R=gloss, G=roughness)</summary>
    <div style="border:1px solid #444;border-radius:6px;padding:12px;margin-top:8px">
      <div id="repackSlots"></div>
      <div style="display:flex;gap:8px;align-items:center;margin-top:8px">
        <label style="font-size:14px">Output channels</label>
        <input id="repackChannels" type="number" min="1" max="4" value="3" style="padding:4px;width:70px">
        <button id="btnRepack" class="btn">Repack &amp; Download PNG</button>
      </div>
    </div>
  </details>

  <pre id="log" style="background:#111;padding:12px;border-radius:6px;margin-top:16px;
       max-height:260px;overflow:auto;font-size:12px;white-space:pre-wrap"></pre>
`;
document.body.style.background = '#0d0d1a';
document.body.appendChild(container);

const styleEl = document.createElement('style');
styleEl.textContent = `
  .btn{display:inline-flex;align-items:center;gap:4px;cursor:pointer;background:#2d2d5e;
    color:#eee;border:1px solid #555;border-radius:5px;padding:7px 14px;font-size:14px;margin:2px}
  .btn.primary{background:#3a5edb;border-color:#4a6ef0}
  .btn:disabled{opacity:.5;cursor:not-allowed}
  #drop.active{border-color:#4a6ef0;background:#22223e}
`;
document.head.appendChild(styleEl);

const els = {
  drop: document.getElementById('drop'),
  folderInput: document.getElementById('folderInput'),
  filesInput: document.getElementById('filesInput'),
  fileList: document.getElementById('fileList'),
  rootSelect: document.getElementById('rootSelect'),
  maxSize: document.getElementById('maxSize'),
  reencode: document.getElementById('reencode'),
  jpegQuality: document.getElementById('jpegQuality'),
  btnConvert: document.getElementById('btnConvert'),
  status: document.getElementById('status'),
  repackSlots: document.getElementById('repackSlots'),
  repackChannels: document.getElementById('repackChannels'),
  btnRepack: document.getElementById('btnRepack'),
  log: document.getElementById('log'),
};

function log(msg) {
  els.log.textContent += `[${new Date().toLocaleTimeString()}] ${msg}\n`;
  els.log.scrollTop = els.log.scrollHeight;
}

function setStatus(s) { els.status.textContent = s; }

function downloadBlob(blob, filename) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.click();
  URL.revokeObjectURL(url);
}

// ---------------------------------------------------------------------------
// State: uploaded files
// ---------------------------------------------------------------------------

let native = null;
let uploaded = []; // [{ path, file }]

async function ensureWasm() {
  if (native) return native;
  log('Loading TinyUSDZ WASM module...');
  native = await loadWasm(() => import('./src/tinyusdz/tinyusdz.js'));
  log('WASM module loaded.');
  return native;
}

function refreshFileList() {
  if (!uploaded.length) {
    els.fileList.style.display = 'none';
    els.btnConvert.disabled = true;
    return;
  }
  els.fileList.style.display = 'block';
  els.fileList.innerHTML = uploaded
    .map(({ path, file }) => `<div>${isImageName(path) ? '🖼️' : '📄'} ${path} <span style="color:#777">(${(file.size / 1024).toFixed(1)} KB)</span></div>`)
    .join('');

  // Populate root USD select.
  const usds = uploaded.filter(u => /\.(usd|usda|usdc|usdz)$/i.test(u.path));
  els.rootSelect.innerHTML = usds.map(u => `<option value="${u.path}">${u.path}</option>`).join('');
  els.btnConvert.disabled = usds.length === 0;
  if (usds.length === 0) {
    setStatus('No USD file found in the upload.');
  } else {
    setStatus(`${uploaded.length} files, ${usds.length} USD layer(s).`);
  }
}

async function addFiles(fileEntries) {
  // fileEntries: array of { path, file }
  for (const e of fileEntries) {
    if (!uploaded.some(u => u.path === e.path)) uploaded.push(e);
  }
  refreshFileList();
}

function entriesFromFileList(files) {
  return Array.from(files).map(file => ({
    path: file.webkitRelativePath || file.name,
    file,
  }));
}

els.folderInput.addEventListener('change', e => addFiles(entriesFromFileList(e.target.files)));
els.filesInput.addEventListener('change', e => addFiles(entriesFromFileList(e.target.files)));

// Drag & drop (supports folders via webkitGetAsEntry).
['dragenter', 'dragover'].forEach(ev =>
  els.drop.addEventListener(ev, e => { e.preventDefault(); els.drop.classList.add('active'); }));
['dragleave', 'drop'].forEach(ev =>
  els.drop.addEventListener(ev, e => { e.preventDefault(); els.drop.classList.remove('active'); }));

els.drop.addEventListener('drop', async e => {
  const items = e.dataTransfer.items;
  const entries = [];
  const walk = async (entry, prefix) => {
    if (entry.isFile) {
      const file = await new Promise((res, rej) => entry.file(res, rej));
      entries.push({ path: prefix + entry.name, file });
    } else if (entry.isDirectory) {
      const reader = entry.createReader();
      // readEntries returns at most 100 entries per call; loop until empty.
      let batch;
      do {
        batch = await new Promise((res, rej) => reader.readEntries(res, rej));
        for (const c of batch) await walk(c, prefix + entry.name + '/');
      } while (batch.length > 0);
    }
  };
  if (items && items.length && items[0].webkitGetAsEntry) {
    for (const it of items) {
      const entry = it.webkitGetAsEntry();
      if (entry) await walk(entry, '');
    }
    await addFiles(entries);
  } else {
    await addFiles(entriesFromFileList(e.dataTransfer.files));
  }
});

// ---------------------------------------------------------------------------
// Convert
// ---------------------------------------------------------------------------

els.btnConvert.addEventListener('click', async () => {
  try {
    els.btnConvert.disabled = true;
    await ensureWasm();

    const rootPath = els.rootSelect.value;
    const fitStrategy = (document.querySelector('input[name="fitStrategy"]:checked') || {}).value || 'size';
    const targetTextureBytes = parseByteSize(document.getElementById('targetSize').value);
    const opts = {
      rootPath,
      maxTextureSize: parseInt(els.maxSize.value, 10) || 0,
      targetTextureBytes,
      fitStrategy,
      reencode: els.reencode.checked,
      jpegQuality: parseInt(els.jpegQuality.value, 10) || 90,
      log,
    };
    if (targetTextureBytes > 0) {
      log(`Fitting textures to ${(targetTextureBytes / 1048576).toFixed(1)} MB via "${fitStrategy}" strategy...`);
    }

    // Read all files into memory.
    const assetMap = new Map();
    for (const { path, file } of uploaded) {
      assetMap.set(path, new Uint8Array(await file.arrayBuffer()));
    }

    setStatus('Converting...');
    const { usdz, stats } = await convertFolderToUSDZ(native, assetMap, opts);
    log(`Done. textures: ${stats.textures}, resized: ${stats.resized}, reencoded: ${stats.reencoded}. USDZ: ${usdz.length} bytes`);

    const base = rootPath.split('/').pop().replace(/\.(usd|usda|usdc|usdz)$/i, '');
    downloadBlob(new Blob([usdz], { type: 'model/vnd.usdz+zip' }), `${base}.usdz`);
    setStatus('USDZ downloaded.');
  } catch (err) {
    log('ERROR: ' + (err && err.message ? err.message : err));
    setStatus('Failed.');
  } finally {
    els.btnConvert.disabled = false;
  }
});

// ---------------------------------------------------------------------------
// Repack tool
// ---------------------------------------------------------------------------

const REPACK_CHANS = ['R', 'G', 'B', 'A'];
els.repackSlots.innerHTML = REPACK_CHANS.map((c, i) => `
  <div style="display:flex;gap:8px;align-items:center;margin:4px 0;font-size:14px">
    <b style="width:18px">${c}</b>
    <label class="btn" style="padding:4px 10px">file<input type="file" data-slot="${i}" accept=".png,.jpg,.jpeg" style="display:none"></label>
    <span data-slotname="${i}" style="color:#999;flex:1">(none)</span>
    <label>src ch</label>
    <select data-slotch="${i}" style="padding:3px"><option>0</option><option>1</option><option>2</option><option>3</option></select>
    <label>or const</label>
    <input type="number" data-slotconst="${i}" min="0" max="255" placeholder="-" style="padding:3px;width:70px">
  </div>`).join('');

const repackFiles = [null, null, null, null];
els.repackSlots.querySelectorAll('input[type=file]').forEach(inp => {
  inp.addEventListener('change', async e => {
    const i = +inp.dataset.slot;
    const f = e.target.files[0];
    repackFiles[i] = f ? new Uint8Array(await f.arrayBuffer()) : null;
    els.repackSlots.querySelector(`[data-slotname="${i}"]`).textContent = f ? f.name : '(none)';
  });
});

els.btnRepack.addEventListener('click', async () => {
  try {
    await ensureWasm();
    const args = { channels: parseInt(els.repackChannels.value, 10) || 3, format: 'png' };
    for (let i = 0; i < 4; i++) {
      const constEl = els.repackSlots.querySelector(`[data-slotconst="${i}"]`);
      const chEl = els.repackSlots.querySelector(`[data-slotch="${i}"]`);
      const key = REPACK_CHANS[i].toLowerCase();
      if (repackFiles[i]) {
        args[key] = { data: repackFiles[i], channel: parseInt(chEl.value, 10) || 0 };
      } else if (constEl.value !== '') {
        args[key] = { const: parseInt(constEl.value, 10) || 0 };
      }
    }
    log('Repacking channels...');
    const res = native.repackChannels(args);
    if (!res.success) throw new Error(res.error);
    log(`Packed ${res.width}x${res.height}x${res.channels}, ${res.data.length} bytes`);
    downloadBlob(new Blob([new Uint8Array(res.data)], { type: 'image/png' }), 'packed.png');
  } catch (err) {
    log('Repack ERROR: ' + (err && err.message ? err.message : err));
  }
});

log('usdzconvert ready. Upload a folder or files to begin.');
