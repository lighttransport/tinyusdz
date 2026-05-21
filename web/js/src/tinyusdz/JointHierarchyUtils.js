// Escape a string for safe interpolation into HTML (text or quoted-attribute
// context). Bone/joint names come from untrusted USD files and must never
// reach innerHTML unescaped.
function escapeHtml(value) {
  return String(value)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

/**
 * Build HTML for a bone hierarchy tree.
 *
 * @param {Array} bones - Array of THREE.Bone
 * @param {Object} options
 * @param {boolean} [options.wrap=true] - Wrap result in outer div
 * @param {string} [options.wrapperStyle] - Inline style for wrapper
 * @param {string} [options.itemClassName='joint-item'] - Class name for each row
 * @param {string} [options.hoverBackground='rgba(255,255,255,0.1)'] - Hover bg color
 * @param {string} [options.rootColor='#ff6b6b'] - Root bone color
 * @param {string} [options.childColor='#4ecdc4'] - Child bone color
 * @returns {string}
 */
export function buildJointHierarchyHTML(bones, options = {}) {
  if (!bones || bones.length === 0) {
    return options.wrap
      ? '<p>No skeleton loaded</p>'
      : '';
  }

  const wrap = options.wrap !== undefined ? options.wrap : true;
  const wrapperStyle =
    options.wrapperStyle ||
    'font-family: monospace; font-size: 12px; line-height: 1.4;';
  const itemClassName = options.itemClassName || 'joint-item';
  const hoverBackground =
    options.hoverBackground || 'rgba(255,255,255,0.1)';
  const rootColor = options.rootColor || '#ff6b6b';
  const childColor = options.childColor || '#4ecdc4';

  let html = wrap ? `<div style="${wrapperStyle}">` : '';

  function traverseBone(bone, depth = 0) {
    const indent = '&nbsp;&nbsp;'.repeat(depth);
    const bullet = depth > 0 ? '+- ' : '* ';
    const color = depth === 0 ? rootColor : childColor;

    html += `<div style="color: ${color}; cursor: pointer; padding: 2px; border-radius: 3px;"
              class="${itemClassName}"
              data-bone-name="${escapeHtml(bone.name)}"
              onmouseover="this.style.backgroundColor='${hoverBackground}'"
              onmouseout="this.style.backgroundColor='transparent'">${indent}${bullet}${escapeHtml(bone.name)}</div>`;

    bone.children.forEach((child) => {
      if (child.isBone) {
        traverseBone(child, depth + 1);
      }
    });
  }

  bones.forEach((bone) => {
    if (!bone.parent || !bone.parent.isBone) {
      traverseBone(bone);
    }
  });

  if (wrap) {
    html += '</div>';
  }

  return html;
}
