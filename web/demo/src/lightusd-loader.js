// Shared WASM loading spinner helpers.

let overlayEl = null;
let progressInterval = null;

/** Create or return the loading overlay. */
function ensureOverlay(parent) {
  if (!overlayEl) {
    overlayEl = document.createElement('div');
    overlayEl.className = 'lightusd-loader hidden';
    overlayEl.innerHTML =
      '<div class="loader-progress"><div class="loader-bar"></div></div>' +
      '<div class="loader-text">Loading LightUSD WASM…</div>';
    overlayEl.style.pointerEvents = 'none';
  }
  if (!overlayEl.parentNode && parent) {
    parent.appendChild(overlayEl);
  }
  return overlayEl;
}

function animateBar() {
  const bar = overlayEl?.querySelector('.loader-bar');
  if (!bar) return;
  let dir = 1;
  let pos = 0;
  clearInterval(progressInterval);
  progressInterval = setInterval(() => {
    pos += dir * 1.2;
    if (pos >= 95) dir = -1;
    if (pos <= 5) dir = 1;
    bar.style.width = pos + '%';
  }, 30);
}

export function showLoader(text, parent) {
  const el = ensureOverlay(parent || document.body);
  const bar = el.querySelector('.loader-bar');
  if (bar) bar.style.width = '10%';
  el.querySelector('.loader-text').textContent = text || 'Loading LightUSD WASM…';
  el.classList.remove('hidden');
  animateBar();
}

export function showLoaderProgress(text, pct, parent) {
  const el = ensureOverlay(parent || document.body);
  const bar = el.querySelector('.loader-bar');
  if (bar) {
    bar.style.width = Math.min(100, Math.max(0, pct)) + '%';
    bar.style.transition = 'width 120ms ease';
  }
  el.querySelector('.loader-text').textContent = text || 'Loading…';
  el.classList.remove('hidden');
  if (progressInterval) { clearInterval(progressInterval); progressInterval = null; }
}

export function hideLoader() {
  if (progressInterval) { clearInterval(progressInterval); progressInterval = null; }
  if (overlayEl) {
    const bar = overlayEl.querySelector('.loader-bar');
    if (bar) bar.style.width = '100%';
    setTimeout(() => overlayEl.classList.add('hidden'), 200);
  }
}
