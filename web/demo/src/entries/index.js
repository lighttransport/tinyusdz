import { DEMOS } from '../demo-configs.js';

function escapeHTML(v) {
  return String(v).replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;');
}

const grid = document.querySelector('[data-demo-grid]');
const searchInput = document.getElementById('demo-search');
const countsEl = document.getElementById('demo-counts');

function renderDemos(filter = '') {
  const q = filter.toLowerCase().trim();
  const filtered = q
    ? DEMOS.filter((d) =>
        d.title.toLowerCase().includes(q) ||
        d.subtitle.toLowerCase().includes(q) ||
        d.id.toLowerCase().includes(q)
      )
    : DEMOS;

  grid.innerHTML = filtered.map((demo) => {
    const image = demo.image
      ? `<img src="${demo.image}" alt="${escapeHTML(demo.title)}">`
      : `<div class="demo-placeholder"><span>${escapeHTML(demo.title)}</span></div>`;
    return `
      <article class="demo-card">
        <div class="demo-image">${image}</div>
        <div class="demo-content">
          <h3 class="demo-title">${escapeHTML(demo.title)}</h3>
          <p class="demo-description">${escapeHTML(demo.subtitle)}</p>
          <div class="demo-links">
            <a href="${demo.href}" class="demo-link">Open Demo</a>
          </div>
        </div>
      </article>
    `;
  }).join('');

  // Update count
  countsEl.innerHTML = DEMOS.length === filtered.length
    ? `<span style="font-size:.82rem;color:var(--dim)">${DEMOS.length} demos</span>`
    : `<span style="font-size:.82rem;color:var(--accent);font-weight:600">${filtered.length}</span><span style="font-size:.82rem;color:var(--dim)"> / ${DEMOS.length} demos</span>`;
}

// Search with debounce
let timer = null;
if (searchInput) {
  searchInput.addEventListener('input', () => {
    clearTimeout(timer);
    timer = setTimeout(() => renderDemos(searchInput.value), 150);
  });
}

renderDemos();
