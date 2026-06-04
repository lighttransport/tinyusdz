// usddiff — TinyUSDZ WASM
// Pick two USD files (.usd/.usda/.usdc/.usdz), diff them at the
// Layer / PrimSpec / Attribute level, and view the result as text or JSON.
//
// Files are loaded as Layers (pre-composition), so the full prim/attribute tree
// is compared. Mirrors the native `tusddiff` tool (tools/tusddiff/tusddiff.cc).

import { loadWasm } from './src/usdzconvert.js';

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

const container = document.createElement('div');
container.style.cssText = 'max-width:900px;margin:32px auto;padding:20px;font-family:system-ui,sans-serif;color:#eee';
container.innerHTML = `
  <h1 style="margin:0 0 6px">TinyUSDZ — usddiff</h1>
  <p style="color:#aaa;margin:0 0 18px">
    Pick two USD files (<b>.usd / .usda / .usdc / .usdz</b>) and compare them at the
    Layer / Prim / Attribute level. Files are diffed pre-composition, so the full
    prim &amp; attribute tree is checked.
  </p>

  <div style="display:grid;grid-template-columns:1fr 1fr;gap:14px;margin-bottom:14px">
    <div id="dropLeft" class="drop">
      <div style="margin-bottom:8px;color:#bbb">Left (file 1)</div>
      <label class="btn">Choose file<input id="leftInput" type="file"
        accept=".usd,.usda,.usdc,.usdz" style="display:none"></label>
      <div id="leftName" style="color:#999;margin-top:8px;font-size:13px">(none)</div>
    </div>
    <div id="dropRight" class="drop">
      <div style="margin-bottom:8px;color:#bbb">Right (file 2)</div>
      <label class="btn">Choose file<input id="rightInput" type="file"
        accept=".usd,.usda,.usdc,.usdz" style="display:none"></label>
      <div id="rightName" style="color:#999;margin-top:8px;font-size:13px">(none)</div>
    </div>
  </div>

  <div style="display:flex;gap:14px;align-items:center;margin-bottom:12px;font-size:14px">
    <span style="color:#bbb">Format:</span>
    <label style="display:inline-flex;gap:4px"><input type="radio" name="fmt" value="text" checked> Text</label>
    <label style="display:inline-flex;gap:4px"><input type="radio" name="fmt" value="json"> JSON</label>
    <button id="btnDiff" class="btn primary" disabled>Compare</button>
    <span id="status" style="color:#aaa"></span>
  </div>

  <pre id="result" style="background:#111;padding:12px;border-radius:6px;
       max-height:60vh;overflow:auto;font-size:12.5px;white-space:pre-wrap;line-height:1.5"></pre>
`;
document.body.style.background = '#0d0d1a';
document.body.appendChild(container);

const styleEl = document.createElement('style');
styleEl.textContent = `
  .btn{display:inline-flex;align-items:center;gap:4px;cursor:pointer;background:#2d2d5e;
    color:#eee;border:1px solid #555;border-radius:5px;padding:7px 14px;font-size:14px;margin:2px}
  .btn.primary{background:#3a5edb;border-color:#4a6ef0}
  .btn:disabled{opacity:.5;cursor:not-allowed}
  .drop{border:2px dashed #555;border-radius:8px;padding:18px;text-align:center;background:#1a1a2e}
  .drop.active{border-color:#4a6ef0;background:#22223e}
  #result .add{color:#7ee787}
  #result .del{color:#ff7b72}
  #result .mod{color:#d2a8ff}
`;
document.head.appendChild(styleEl);

const els = {
  dropLeft: document.getElementById('dropLeft'),
  dropRight: document.getElementById('dropRight'),
  leftInput: document.getElementById('leftInput'),
  rightInput: document.getElementById('rightInput'),
  leftName: document.getElementById('leftName'),
  rightName: document.getElementById('rightName'),
  btnDiff: document.getElementById('btnDiff'),
  status: document.getElementById('status'),
  result: document.getElementById('result'),
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

let native = null;
const files = { left: null, right: null }; // { name, data: Uint8Array }

async function ensureWasm() {
  if (native) return native;
  setStatus('Loading WASM…');
  native = await loadWasm(() => import('./src/tinyusdz/tinyusdz.js'));
  setStatus('');
  return native;
}

function setStatus(s) { els.status.textContent = s; }

function refresh() {
  els.leftName.textContent = files.left ? files.left.name : '(none)';
  els.rightName.textContent = files.right ? files.right.name : '(none)';
  els.btnDiff.disabled = !(files.left && files.right);
}

async function setFile(slot, file) {
  if (!file) { files[slot] = null; refresh(); return; }
  files[slot] = { name: file.name, data: new Uint8Array(await file.arrayBuffer()) };
  refresh();
}

els.leftInput.addEventListener('change', e => setFile('left', e.target.files[0]));
els.rightInput.addEventListener('change', e => setFile('right', e.target.files[0]));

// Drag & drop onto each panel.
function wireDrop(el, slot) {
  ['dragenter', 'dragover'].forEach(ev =>
    el.addEventListener(ev, e => { e.preventDefault(); el.classList.add('active'); }));
  ['dragleave', 'drop'].forEach(ev =>
    el.addEventListener(ev, e => { e.preventDefault(); el.classList.remove('active'); }));
  el.addEventListener('drop', e => {
    const f = e.dataTransfer.files && e.dataTransfer.files[0];
    if (f) setFile(slot, f);
  });
}
wireDrop(els.dropLeft, 'left');
wireDrop(els.dropRight, 'right');

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

function escapeHtml(s) {
  return s.replace(/[&<>]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;' }[c]));
}

// Colorize unified-diff-style text output by leading marker.
function renderText(text) {
  const html = text.split('\n').map(line => {
    const esc = escapeHtml(line);
    if (/^\+/.test(line)) return `<span class="add">${esc}</span>`;
    if (/^- /.test(line) || /^-\//.test(line)) return `<span class="del">${esc}</span>`;
    if (/^~/.test(line)) return `<span class="mod">${esc}</span>`;
    return esc;
  }).join('\n');
  els.result.innerHTML = html;
}

els.btnDiff.addEventListener('click', async () => {
  try {
    els.btnDiff.disabled = true;
    await ensureWasm();
    const format = (document.querySelector('input[name="fmt"]:checked') || {}).value || 'text';
    setStatus('Comparing…');

    const res = native.usddiff({
      left: { data: files.left.data, name: files.left.name },
      right: { data: files.right.data, name: files.right.name },
      format,
    });

    if (!res || !res.success) {
      els.result.textContent = 'Error: ' + (res && res.error ? res.error : 'usddiff failed');
      setStatus('Failed.');
      return;
    }
    if (res.warn) console.warn('usddiff warning:', res.warn);

    if (format === 'json') {
      els.result.textContent = res.json;
    } else {
      renderText(res.text);
    }
    setStatus(res.hasDiffs ? 'Differences found.' : 'No differences.');
  } catch (err) {
    els.result.textContent = 'Error: ' + (err && err.message ? err.message : err);
    setStatus('Failed.');
  } finally {
    els.btnDiff.disabled = !(files.left && files.right);
  }
});

setStatus('Pick two USD files to compare.');
