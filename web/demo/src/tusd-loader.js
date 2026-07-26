// Shared WASM loading spinner helpers.

let overlayEl = null;

/** Create or return the loading overlay. */
function ensureOverlay(parent) {
  if (!overlayEl) {
    overlayEl = document.createElement('div');
    overlayEl.className = 'tusd-loader hidden';
    overlayEl.innerHTML = '<div class="spinner"></div><div class="loader-text">Loading TinyUSDZ WASM…</div>';
    overlayEl.style.pointerEvents = 'none';
  }
  if (!overlayEl.parentNode && parent) {
    parent.appendChild(overlayEl);
  }
  return overlayEl;
}

export function showLoader(text, parent) {
  const el = ensureOverlay(parent || document.body);
  el.querySelector('.loader-text').textContent = text || 'Loading TinyUSDZ WASM…';
  el.classList.remove('hidden');
}

export function hideLoader() {
  if (overlayEl) overlayEl.classList.add('hidden');
}
