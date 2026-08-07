#!/usr/bin/env node
/**
 * Generate the full USD assets manifest from a local checkout of usd-wg/assets.
 *
 * Scans the local directory tree for all .usd/.usda/.usdc/.usdz files,
 * classifies them by category and tags, and writes src/usd-assets-manifest.js.
 *
 * Usage:
 *   USD_ASSETS_DIR=/path/to/usd-wg/assets node scripts/generate-asset-manifest.js
 *
 * USD_ASSETS_DIR or USD_WG_ASSETS_DIR must point to the prepared checkout.
 */

import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { readdirSync, statSync } from 'fs';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const configuredAssetsDir = process.env.USD_ASSETS_DIR || process.env.USD_WG_ASSETS_DIR;
if (!configuredAssetsDir) {
  console.error('Set USD_ASSETS_DIR or USD_WG_ASSETS_DIR to a USD-WG assets checkout.');
  process.exit(2);
}
const ASSETS_DIR = path.resolve(configuredAssetsDir);
const OUT = path.resolve(__dirname, '..', 'src', 'usd-assets-manifest.js');

// ── Category definitions ──
// Each entry: { prefixes: string[], id: string, label: string, tags: string[] }
// `prefixes` are matched against the start of the repoPath.
const CATEGORY_RULES = [
  { prefixes: ['schemaTests/usdGeom/primitives'], id: 'primitives', label: 'Primitives', tags: ['usdGeom', 'mesh'] },
  { prefixes: ['schemaTests/usdGeom/meshes'], id: 'meshes', label: 'Meshes', tags: ['usdGeom', 'mesh'] },
  { prefixes: ['schemaTests/usdGeom/transforms'], id: 'transforms', label: 'Transforms', tags: ['usdGeom', 'xform'] },
  { prefixes: ['schemaTests/usdGeom/extent'], id: 'extent', label: 'Extent', tags: ['usdGeom', 'extent'] },
  { prefixes: ['foundation/stage_configuration'], id: 'foundation_stage', label: 'Stage Config', tags: ['foundation', 'stage'] },
  { prefixes: ['foundation/stage_composition'], id: 'composition', label: 'Composition', tags: ['foundation', 'composition'] },
  { prefixes: ['_common'], id: 'common', label: 'Common Assets', tags: ['common', 'utility'] },
  { prefixes: ['USDZ'], id: 'usdz', label: 'USDZ Samples', tags: ['usdz', 'gltf'] },
  { prefixes: ['MaterialXTest'], id: 'textured_materials', label: 'Textured & Materials', tags: ['texture', 'materialx'] },
  { prefixes: ['TextureTransformTest'], id: 'textured_materials', label: 'Textured & Materials', tags: ['texture', 'transform'] },
  { prefixes: ['TextureCoordinateTest'], id: 'textured_materials', label: 'Textured & Materials', tags: ['texture', 'coordinate'] },
  { prefixes: ['TextureFileFormatTests'], id: 'textured_materials', label: 'Textured & Materials', tags: ['texture', 'format'] },
  { prefixes: ['AlphaBlendModeTest'], id: 'textured_materials', label: 'Textured & Materials', tags: ['alpha', 'blend'] },
  { prefixes: ['AlphaBlendSortTest'], id: 'textured_materials', label: 'Textured & Materials', tags: ['alpha', 'sort'] },
  { prefixes: ['NormalsTextureBiasAndScale'], id: 'textured_materials', label: 'Textured & Materials', tags: ['normal', 'texture'] },
  { prefixes: ['RoughnessTest'], id: 'textured_materials', label: 'Textured & Materials', tags: ['roughness', 'pbr'] },
  { prefixes: ['RelationshipEncapsulationTests'], id: 'relationship_encapsulation', label: 'Relationship Encapsulation', tags: ['relationship', 'encapsulation'] },
  { prefixes: ['References'], id: 'references_test', label: 'References Tests', tags: ['reference', 'override'] },
  { prefixes: ['full_assets/Teapot'], id: 'full_teapot', label: 'Teapot', tags: ['full', 'classic'] },
  { prefixes: ['full_assets/StandardShaderBall'], id: 'full_shaderball', label: 'Shader Ball', tags: ['full', 'materials'] },
  { prefixes: ['full_assets/OpenChessSet'], id: 'full_chess', label: 'Chess Set', tags: ['full', 'composition'] },
  { prefixes: ['full_assets/Vehicles'], id: 'full_vehicles', label: 'Vehicles', tags: ['full', 'vehicle'] },
  { prefixes: ['full_assets/CarbonFrameBike'], id: 'full_other', label: 'Other Full Assets', tags: ['full', 'vehicle'] },
  { prefixes: ['full_assets/UsdCookie'], id: 'full_other', label: 'Other Full Assets', tags: ['full', 'food'] },
  { prefixes: ['full_assets/ElephantWithMonochord'], id: 'full_other', label: 'Other Full Assets', tags: ['full', 'character'] },
  { prefixes: ['full_assets/McUsd'], id: 'full_other', label: 'Other Full Assets', tags: ['full', 'food'] },
  { prefixes: ['full_assets/SubdivisionSurfaces'], id: 'full_other', label: 'Other Full Assets', tags: ['full', 'subdivision'] },
  { prefixes: ['intent-vfx'], id: 'intent_vfx', label: 'Intent VFX', tags: ['intent', 'vfx'] },
];

const TAG_RULES = [
  { match: /subdiv|subdivision/, tags: ['subdivision'] },
  { match: /normals/, tags: ['normals'] },
  { match: /points/, tags: ['points'] },
  { match: /skinning|skin/, tags: ['skinning'] },
  { match: /animat/, tags: ['animation'] },
  { match: /usdz/, tags: ['usdz'] },
  { match: /upAxis|up_axis/, tags: ['upAxis'] },
  { match: /timeCode|time_code|timecode/, tags: ['timecode'] },
  { match: /frame/, tags: ['frames'] },
  { match: /metersPerUnit|meters_per_unit/, tags: ['metersPerUnit'] },
  { match: /reference/, tags: ['reference', 'arc'] },
  { match: /payload/, tags: ['payload', 'arc'] },
  { match: /sublayer|sub_layer/, tags: ['sublayer', 'arc'] },
  { match: /inherit/, tags: ['inherit', 'arc'] },
  { match: /specialize/, tags: ['specialize', 'arc'] },
  { match: /active/, tags: ['active'] },
  { match: /materialx|mtlx/, tags: ['materialx'] },
  { match: /alpha|blend/, tags: ['alpha'] },
  { match: /texture/, tags: ['texture'] },
  { match: /roughness/, tags: ['roughness'] },
  { match: /transform/, tags: ['transform'] },
  { match: /scopes/, tags: ['scope'] },
  { match: /extent/, tags: ['extent', 'bounds'] },
  { match: /empty/, tags: ['empty'] },
  { match: /doubleSided|double_sided/, tags: ['double-sided'] },
  { match: /singleSided|single_sided/, tags: ['single-sided'] },
  { match: /quad/, tags: ['quad'] },
  { match: /triangle/, tags: ['triangle'] },
  { match: /all_primitive/, tags: ['showcase'] },
  { match: /invalid/, tags: ['invalid', 'error-case'] },
  { match: /_stage/, tags: ['stage-root'] },
  { match: /_child/, tags: ['child-layer'] },
  { match: /_parent/, tags: ['parent-layer'] },
  { match: /Asset$|_asset/, tags: ['assembly'] },
  { match: /Geo$|_geo/, tags: ['geometry'] },
  { match: /Look$|_look|Material/, tags: ['material'] },
  { match: /Payload$|_payload/, tags: ['payload'] },
  { match: /Body/, tags: ['body'] },
  { match: /FullAsset/, tags: ['full-assembly'] },
  { match: /variant/i, tags: ['variant'] },
  { match: /purpose/, tags: ['purpose'] },
  { match: /over\./, tags: ['over'] },
  { match: /class_inherit/, tags: ['class', 'inherit'] },
  { match: /thumbnail/, tags: ['thumbnail'] },
  { match: /DrawMode/, tags: ['draw-mode'] },
  { match: /Crease/, tags: ['crease', 'subdivision'] },
  { match: /chess/i, tags: ['game'] },
  { match: /^Teapot/, tags: ['teapot', 'classic'] },
  { match: /CesiumMan/, tags: ['character'] },
  { match: /BrainStem/, tags: ['character'] },
  { match: /Rigged/, tags: ['character', 'skinning'] },
  { match: /DamagedHelmet/, tags: ['helmet'] },
  { match: /Interpolation/, tags: ['interpolation'] },
];

function classify(repoPath, filename) {
  const name = filename.replace(/\.(usd|usda|usdc|usdz)$/i, '');
  const fullName = repoPath + '/' + filename;
  // Normalize for prefix matching: test_assets/foo → foo, full_assets/foo → full_assets/foo
  const normalPath = repoPath.startsWith('test_assets/') ? repoPath.slice('test_assets/'.length) : repoPath;

  let catId = 'uncategorized';
  let catLabel = 'Uncategorized';
  let catTags = [];
  for (const rule of CATEGORY_RULES) {
    if (rule.prefixes.some((pref) => normalPath.startsWith(pref))) {
      catId = rule.id;
      catLabel = rule.label;
      catTags = [...rule.tags];
      break;
    }
  }

  const extraTags = new Set(catTags);
  for (const rule of TAG_RULES) {
    if (rule.match.test(name) || rule.match.test(fullName)) {
      for (const t of rule.tags) extraTags.add(t);
    }
  }
  if (/\.usdz$/i.test(filename)) extraTags.add('usdz');
  if (/\.usdc$/i.test(filename)) extraTags.add('usdc');

  return { catId, catLabel, tags: [...extraTags].sort() };
}

function slug(name) {
  return name.replace(/\.(usd|usda|usdc|usdz)$/i, '').replace(/[^a-zA-Z0-9_-]/g, '_');
}

function humanName(name) {
  return name.replace(/\.(usd|usda|usdc|usdz)$/i, '').replace(/[_-]/g, ' ');
}

function guessDescription(name, repoPath) {
  const base = name.replace(/\.(usd|usda|usdc|usdz)$/i, '');
  const parts = repoPath.split('/').filter(Boolean);
  const ctx = parts.slice(-2).join(' > ').replace(/_/g, ' ');
  return `${ctx}: ${base.replace(/[_-]/g, ' ')}`;
}

// ── Main ──

async function main() {
  if (!fs.existsSync(ASSETS_DIR)) {
    console.error(`USD assets directory not found: ${ASSETS_DIR}`);
    console.error('Set USD_ASSETS_DIR to point to a checkout of https://github.com/usd-wg/assets');
    process.exit(1);
  }

  console.log(`Scanning ${ASSETS_DIR}...`);

  function walk(dir, relativePath) {
    const results = [];
    let entries;
    try { entries = readdirSync(dir); } catch { return results; }
    for (const name of entries) {
      if (name.startsWith('.') || name === 'node_modules' || name === 'docs' || name === 'scripts') continue;
      const full = path.join(dir, name);
      const rel = relativePath ? relativePath + '/' + name : name;
      let st;
      try { st = statSync(full); } catch { continue; }
      if (st.isDirectory()) {
        results.push(...walk(full, rel));
      } else if (st.isFile() && /\.(usd|usda|usdc|usdz)$/i.test(name)) {
        results.push(rel);
      }
    }
    return results;
  }

  const allFiles = walk(ASSETS_DIR, '');
  const unique = [...new Set(allFiles)].sort();
  console.log(`Found ${unique.length} USD file(s).`);

  // Map category ids to labels
  const catMap = new Map();
  for (const rule of CATEGORY_RULES) {
    if (!catMap.has(rule.id)) catMap.set(rule.id, { id: rule.id, label: rule.label });
  }

  const entries = unique.map((file) => {
    const repoPath = path.dirname(file);
    const filename = path.basename(file);
    const { catId, catLabel, tags } = classify(repoPath, filename);
    const name = humanName(filename);
    const id = slug(filename);
    const desc = guessDescription(filename, repoPath);
    if (!catMap.has(catId)) catMap.set(catId, { id: catId, label: catLabel });
    return {
      id,
      name,
      category: catId,
      tags,
      repoPath,
      filename,
      description: desc,
    };
  });

  entries.sort((a, b) => {
    if (a.category !== b.category) return a.category.localeCompare(b.category);
    return a.name.localeCompare(b.name);
  });

  const cats = [...catMap.values()].sort((a, b) => a.id.localeCompare(b.id));

  const code = `// Auto-generated by scripts/generate-asset-manifest.js
// Source: ${ASSETS_DIR}
// Generated: ${new Date().toISOString()}
// Total assets: ${entries.length}

export const CATEGORIES = ${JSON.stringify(cats, null, 2)};

export const ASSETS = ${JSON.stringify(entries, null, 2)};
`;

  fs.mkdirSync(path.dirname(OUT), { recursive: true });
  fs.writeFileSync(OUT, code, 'utf-8');
  console.log(`\nManifest written to ${OUT}`);
  console.log(`Categories: ${cats.length}`);
  console.log(`Assets: ${entries.length}`);
}

main().catch((e) => {
  console.error('Fatal:', e.message);
  process.exit(1);
});
