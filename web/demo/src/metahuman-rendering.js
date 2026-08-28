import * as THREE from 'three';
import { DEFAULT_HAIR, HairFurRenderer, buildHairRibbonGeometry } from './hair-fur-rendering.js';

function materialValue(material, key, fallback) {
  const data = material?.userData?.openPBRData || material?.userData?.rawData || {};
  const surface = data.openPBR || data.openPBRShader || data.openPBRSurface || data;
  const value = surface[key] ?? surface.inputs?.[key] ?? fallback;
  return value;
}

function asColor(value, fallback) {
  if (value instanceof THREE.Color) return value;
  if (Array.isArray(value)) return new THREE.Color(value[0] ?? 1, value[1] ?? 1, value[2] ?? 1);
  return new THREE.Color(fallback);
}

function isSkinMaterial(material, object) {
  const label = `${material?.name || ''} ${object.name || ''} ${object.userData?.usdMesh?.primPath || ''}`.toLowerCase();
  return /skin|face|head|body/.test(label) || Number(materialValue(material, 'subsurface_weight', 0)) > 0;
}

function patchSkinMaterial(material) {
  if (!material || material.userData?.metaHumanSkin) return;
  const weight = Number(materialValue(material, 'subsurface_weight', 0.0)) || 0;
  const radius = Number(materialValue(material, 'subsurface_radius', 0.0)) || 0;
  if (weight <= 0 && !/skin/i.test(material.name || '')) return;
  const color = asColor(materialValue(material, 'subsurface_color', [1.0, 0.28, 0.14]), 0xff6840);
  const scale = asColor(materialValue(material, 'subsurface_radius_scale', [1.0, 0.35, 0.18]), 0xff6b35);
  material.userData.metaHumanSkin = { weight: Math.min(1, weight || 0.55), radius: Math.max(0.01, radius || 0.16), color, scale };
  const priorCompile = material.onBeforeCompile;
  material.onBeforeCompile = (shader, renderer) => {
    priorCompile?.(shader, renderer);
    shader.uniforms.mhSssColor = { value: color };
    shader.uniforms.mhSssRadiusScale = { value: scale };
    shader.uniforms.mhSssWeight = { value: material.userData.metaHumanSkin.weight };
    shader.uniforms.mhSssRadius = { value: material.userData.metaHumanSkin.radius };
    shader.fragmentShader = shader.fragmentShader.replace(
      '#include <output_fragment>',
      `// Bounded raster SSS: a view/light-wrap diffusion term, intentionally not a screen-space blur.
       float mhWrap = pow(clamp(1.0 - dot(normalize(normal), normalize(vViewPosition)), 0.0, 1.0), 1.45);
       vec3 mhScatter = mhSssColor * mhSssRadiusScale * mhSssWeight * mhSssRadius * (0.16 + 0.84 * mhWrap);
       outgoingLight += mhScatter;
       #include <output_fragment>`
    );
  };
  material.needsUpdate = true;
}

function curveData(curves) {
  const controls = curves.points?.length || 0;
  const tessellated = curves.tessellatedPoints?.length || 0;
  if (!controls || tessellated <= controls * 2) return curves;
  return { ...curves, tessellatedPoints: curves.points, tessellatedVertexCounts: curves.curveVertexCounts,
    tessellatedWidths: curves.widths, tessellatedColors: curves.colors };
}

function hairParameters(usd, curves) {
  const material = curves.materialId >= 0 ? (usd.getMaterial?.(curves.materialId) || usd.materials?.[curves.materialId]) : null;
  return { ...DEFAULT_HAIR, ...(material?.hair || {}) };
}

function deferredSummary(app, usd) {
  const files = app.localExportFiles || new Map();
  const archive = usd?.archiveEntries || new Map();
  const has = (name) => [...files.keys(), ...archive.keys()].some((key) => key.endsWith(name));
  return [
    has('MetaHuman_Deformers.usda')
      ? 'DNA/RigLogic deformer metadata found: deferred (generic USD blendshapes still play).'
      : 'DNA/RigLogic deformer data is not loaded; generic USD blendshapes still play when authored.',
    has('MetaHuman_Physics.usda')
      ? 'Unreal PhysicsAsset metadata found: deferred (no ragdoll simulation).'
      : 'PhysicsAsset simulation is deferred.'
  ];
}

export async function installMetaHumanRendering(app) {
  if (!app.renderer.capabilities.isWebGL2) throw new Error('MetaHuman rendering requires WebGL 2.');
  const hair = new HairFurRenderer(app);
  app.keyLight.castShadow = true;
  app.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  app.keyLight.shadow.mapSize.set(4096, 4096);
  Object.assign(app.keyLight.shadow.camera, { left: -130, right: 130, top: 180, bottom: -80, near: 1, far: 600 });
  const params = { hairWidth: 1.0, hairOpacity: 0.78, hairExposure: 1.2 };
  const folder = app.gui.addFolder('MetaHuman quality');
  folder.add(params, 'hairWidth', 0.35, 2.5, 0.01).name('Strand width').onChange(updateHair);
  folder.add(params, 'hairOpacity', 0.1, 1.0, 0.01).name('Hair opacity').onChange(updateHair);
  folder.add(params, 'hairExposure', 0.4, 2.5, 0.01).name('Hair exposure').onChange((v) => { hair.composite.uniforms.exposure.value = v; });
  folder.open();
  function updateHair() {
    for (const mesh of hair.hairMeshes) {
      for (const shader of [mesh.userData.hairAccum, mesh.userData.hairReveal]) {
        shader.uniforms.widthScale.value = params.hairWidth;
        shader.uniforms.opacity.value = params.hairOpacity;
      }
      mesh.userData.hairDepth.uniforms.widthScale.value = params.hairWidth;
      mesh.userData.hairDepth.uniforms.opacity.value = params.hairOpacity;
    }
  }
  function installScene({ usd, root }) {
    hair.clear();
    app.scene.environment = app.envMap;
    app.scene.background = app.environmentSource || new THREE.Color(0x0e0e10);
    root.traverse((object) => {
      if (object.isMesh) {
        object.castShadow = true;
        object.receiveShadow = true;
        const materials = Array.isArray(object.material) ? object.material : [object.material];
        for (const material of materials) if (isSkinMaterial(material, object)) patchSkinMaterial(material);
      }
      const meta = object.userData?.usdCurves;
      if (!meta) return;
      const curves = usd.getCurves?.(meta.index) || usd.curves?.[meta.index];
      if (!curves) return;
      object.visible = false;
      const ribbons = new THREE.Mesh(buildHairRibbonGeometry(curveData(curves)));
      ribbons.name = `${curves.name || 'Groom'} high-fidelity ribbons`;
      object.parent?.add(ribbons);
      hair.add(ribbons, hairParameters(usd, curves), { widthScale: params.hairWidth, opacity: params.hairOpacity, environmentIntensity: 1.35 });
    });
    updateHair();
    app.setNotes([
      'WebGL2 maximum-quality preset: full exported groom density, half-float weighted transparency, and 4096px shadow map.',
      'MaterialX/OpenPBR skin uses a bounded UE-inspired wrapped-diffusion SSS approximation; it is not pixel-identical UE subsurface scattering.',
      ...deferredSummary(app, usd)
    ]);
    usd.releaseBuildData?.();
  }
  app.onSceneChanged(installScene);
  if (app.currentUsd) installScene({ usd: app.currentUsd, root: app.world });
  app.renderOverride = () => hair.render();
  window.__metaHumanRendering = { hair, params };
}
