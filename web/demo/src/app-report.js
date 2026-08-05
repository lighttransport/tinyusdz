/*
 * Unified loading progress, error reporting, and warning display.
 *
 * Usage:
 *   import { Report } from '../app-report.js';
 *
 *   // Multi-step loading
 *   await Report.steps('Loading Scene', [
 *     { label: 'WASM module', run: () => loader.init() },
 *     { label: 'Parsing USD', run: () => loader.parse(data, ...) },
 *     { label: 'Building 3D', run: () => buildThreeNode(...) },
 *   ]);
 *
 *   // Error with action
 *   Report.err(e).action('Retry', () => loadAgain());
 *
 *   // Warning
 *   Report.warn('Texture not found', 'Using fallback material');
 *
 *   // Hide everything
 *   Report.done();
 */

import { showLoader, hideLoader, showLoaderProgress } from './tusd-loader.js';

let errorPanel = null;
let warnBadge = null;
let warnCount = 0;

// ── Error panel ──

function ensureErrorPanel() {
  if (errorPanel) return errorPanel;
  errorPanel = document.createElement('div');
  errorPanel.id = 'tusd-error';
  errorPanel.style.cssText =
    'position:fixed;bottom:16px;left:50%;transform:translateX(-50%);z-index:100;' +
    'max-width:min(680px,calc(100vw-32px));width:100%;' +
    'background:rgba(240,138,138,.10);border:1px solid rgba(240,138,138,.35);' +
    'border-radius:8px;padding:0;overflow:hidden;' +
    'font-family:system-ui;font-size:.8rem;line-height:1.4;' +
    'transition:opacity .25s ease,transform .25s ease;' +
    'opacity:0;transform:translateX(-50%) translateY(10px);' +
    'pointer-events:none';
  errorPanel.innerHTML = `
    <div style="padding:10px 12px;display:flex;align-items:flex-start;gap:10px">
      <span style="flex:0 0 auto;font-size:1rem;color:#f08a8a;line-height:1.35" id="tusd-error-icon">✗</span>
      <div style="flex:1;min-width:0" id="tusd-error-body">
        <div style="color:#f08a8a;font-weight:600;margin-bottom:2px" id="tusd-error-title">Error</div>
        <div style="color:#d4c5c5;font-size:.76rem;word-break:break-word" id="tusd-error-msg"></div>
      </div>
      <button style="flex:0 0 auto;background:none;border:1px solid rgba(240,138,138,.3);border-radius:4px;color:#f08a8a;cursor:pointer;padding:3px 10px;font-size:.76rem;display:none" id="tusd-error-action">Retry</button>
      <button style="flex:0 0 auto;background:none;border:none;color:#a08080;cursor:pointer;padding:3px;font-size:.9rem;line-height:1" id="tusd-error-close">✕</button>
    </div>`;
  document.body.appendChild(errorPanel);

  errorPanel.querySelector('#tusd-error-close').addEventListener('click', () => {
    errorPanel.style.opacity = '0';
    errorPanel.style.transform = 'translateX(-50%) translateY(10px)';
    errorPanel.style.pointerEvents = 'none';
  });

  return errorPanel;
}

function ensureWarnBadge() {
  if (warnBadge) return warnBadge;
  warnBadge = document.createElement('div');
  warnBadge.id = 'tusd-warn-badge';
  warnBadge.style.cssText =
    'position:fixed;top:12px;right:12px;z-index:100;' +
    'display:none;align-items:center;gap:5px;' +
    'padding:5px 10px;border-radius:6px;' +
    'background:rgba(242,200,121,.12);border:1px solid rgba(242,200,121,.3);' +
    'color:#f2c879;font-family:system-ui;font-size:.72rem;font-weight:600;' +
    'cursor:pointer;transition:opacity .2s';
  warnBadge.innerHTML = '⚠ <span id="tusd-warn-count">0</span> warnings';
  warnBadge.addEventListener('click', () => {
    const list = document.getElementById('tusd-warn-list');
    if (list) list.style.display = list.style.display === 'none' ? 'block' : 'none';
  });
  document.body.appendChild(warnBadge);
  return warnBadge;
}

// ── Public API ──

export const Report = {
  /** Run a series of labeled async steps with progress bar. */
  async steps(title, steps) {
    showLoader(title || 'Loading…', document.querySelector('.viewport-wrap, .assets-viewport-wrap, main') || document.body);
    for (let i = 0; i < steps.length; i++) {
      const step = steps[i];
      const pct = Math.round(((i) / steps.length) * 100);
      showLoaderProgress(`${step.label}…`, pct);
      try {
        await step.run();
      } catch (e) {
        showLoaderProgress(`${step.label} — failed`, 100);
        this.err(e);
        throw e;
      }
    }
    this.done();
  },

  /** Set progress percentage (0-100). */
  progress(pct, label) {
    showLoaderProgress(label || `Loading… (${pct}%)`, pct);
  },

  /** Show error panel. Returns object with .action() to add retry button. */
  err(error, context) {
    const panel = ensureErrorPanel();
    const title = panel.querySelector('#tusd-error-title');
    const msg = panel.querySelector('#tusd-error-msg');
    const actionBtn = panel.querySelector('#tusd-error-action');
    const icon = panel.querySelector('#tusd-error-icon');

    title.textContent = error?.message || String(error);
    msg.textContent = context ? `${context}: ${error?.message || error}` : '';
    actionBtn.style.display = 'none';
    icon.textContent = '✗';

    panel.style.opacity = '1';
    panel.style.transform = 'translateX(-50%) translateY(0)';
    panel.style.pointerEvents = 'auto';

    // Also log to console
    if (error instanceof Error) console.error(error);
    else console.error(error);

    return {
      action(label, fn) {
        actionBtn.textContent = label;
        actionBtn.style.display = 'block';
        actionBtn.onclick = () => { this.dismiss(); fn(); };
        return this;
      },
      dismiss() {
        panel.style.opacity = '0';
        panel.style.transform = 'translateX(-50%) translateY(10px)';
        panel.style.pointerEvents = 'none';
      },
    };
  },

  /** Show warning badge. */
  warn(msg, detail) {
    warnCount++;
    const badge = ensureWarnBadge();
    badge.style.display = 'flex';
    badge.querySelector('#tusd-warn-count').textContent = String(warnCount);

    // Build warning list
    let list = document.getElementById('tusd-warn-list');
    if (!list) {
      list = document.createElement('div');
      list.id = 'tusd-warn-list';
      list.style.cssText =
        'position:fixed;top:44px;right:12px;z-index:100;' +
        'max-width:420px;max-height:50vh;overflow-y:auto;' +
        'background:rgba(20,23,28,.95);border:1px solid rgba(242,200,121,.25);' +
        'border-radius:8px;padding:8px;font-size:.74rem;line-height:1.35;' +
        'display:none';
      document.body.appendChild(list);
    }

    const row = document.createElement('div');
    row.style.cssText = 'padding:4px 8px;margin-bottom:4px;background:rgba(242,200,121,.06);border-radius:4px';
    row.innerHTML = `<span style="color:#f2c879;font-weight:600">${msg}</span>${detail ? `<span style="color:#a09880;display:block;margin-top:2px">${detail}</span>` : ''}`;
    list.appendChild(row);

    console.warn(msg, detail);
  },

  /** Hide loading and keep error/warning displays. */
  done() {
    hideLoader();
  },

  /** Hide everything (loading + errors). */
  dismiss() {
    hideLoader();
    if (errorPanel) {
      errorPanel.style.opacity = '0';
      errorPanel.style.transform = 'translateX(-50%) translateY(10px)';
      errorPanel.style.pointerEvents = 'none';
    }
  },

  /** Reset warning count and hide badge. */
  clear() {
    warnCount = 0;
    if (warnBadge) { warnBadge.style.display = 'none'; }
    const list = document.getElementById('tusd-warn-list');
    if (list) list.remove();
    this.dismiss();
  },
};
