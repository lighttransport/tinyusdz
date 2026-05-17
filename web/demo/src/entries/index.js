import { DEMOS } from '../demo-configs.js';

const grid = document.querySelector('[data-demo-grid]');

grid.innerHTML = DEMOS.map((demo) => {
  const image = demo.image
    ? `<img src="${demo.image}" alt="${demo.title}">`
    : `<div class="demo-placeholder"><span>${demo.title}</span></div>`;
  return `
    <article class="demo-card">
      <div class="demo-image">${image}</div>
      <div class="demo-content">
        <h3 class="demo-title">${demo.title}</h3>
        <p class="demo-description">${demo.subtitle}</p>
        <div class="demo-links">
          <a href="${demo.href}" class="demo-link">Open Demo</a>
        </div>
      </div>
    </article>
  `;
}).join('');
