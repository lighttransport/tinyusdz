export const DEMOS = [
  {
    id: 'materialx-node',
    title: 'MaterialX Node Graph',
    subtitle: 'OpenPBR / MaterialX material path with a compact node graph inspector.',
    defaultAsset: './assets/fancy-teapot-mtlx.usdz',
    preferredMaterialType: 'openpbr',
    materialBackend: 'nodegraph',
    materialModeLabel: 'OpenPBR node graph',
    enableMaterialGraph: true,
    useUsdLux: true,
    image: './assets/previews/materialx-node.jpg',
    href: './materialx-node.html'
  },
  {
    id: 'materialx-physical',
    title: 'MaterialX MeshPhysicalMaterial',
    subtitle: 'MaterialX / USD material data converted to Three.js MeshPhysicalMaterial.',
    defaultAsset: './assets/fancy-teapot-mtlx.usdz',
    preferredMaterialType: 'usdpreviewsurface',
    materialBackend: 'meshphysical',
    materialModeLabel: 'MeshPhysicalMaterial',
    useUsdLux: true,
    image: './assets/previews/materialx-physical.jpg',
    href: './materialx-physical.html'
  },
  {
    id: 'usdlux',
    title: 'USDLux Lighting',
    subtitle: 'Loads UsdLux lights, DomeLight environments, and direct light data.',
    defaultAsset: './assets/envmap-constant-test.usdz',
    preferredMaterialType: 'auto',
    useUsdLux: true,
    useDefaultLights: false,
    image: './assets/previews/usdlux.jpg',
    href: './usdlux.html'
  },
  {
    id: 'skinning',
    title: 'Skinning',
    subtitle: 'Builds skeletons and binds CesiumMan USDZ skinned meshes with helpers visible.',
    defaultAsset: 'https://raw.githubusercontent.com/usd-wg/assets/refs/heads/main/test_assets/USDZ/CesiumMan/CesiumMan.usdz',
    preferredMaterialType: 'auto',
    enableSkinning: true,
    showSkeleton: true,
    image: './assets/previews/skinning.jpg',
    href: './skinning.html'
  },
  {
    id: 'xform-skinning-animation',
    title: 'Node Xform + SkelAnimation',
    subtitle: 'Plays node xform animation together with skeletal animation clips.',
    defaultAsset: './assets/multi-clip-skeleton.usda',
    preferredMaterialType: 'auto',
    enableSkinning: true,
    showSkeleton: true,
    enableAnimation: true,
    image: './assets/previews/xform-skinning-animation.jpg',
    href: './xform-skinning-animation.html'
  },
  {
    id: 'physics',
    title: 'USD Physics',
    subtitle: 'Loads a physics-authored robot arm scene for inspection and rendering.',
    defaultAsset: './assets/physics-robot-arm.usda',
    preferredMaterialType: 'auto',
    image: './assets/previews/physics.jpg',
    href: './physics.html'
  },
  {
    id: 'asset-resolver',
    title: 'Asset Resolver Textures',
    subtitle: 'Loads a textured cat plane and resolves referenced texture assets.',
    defaultAsset: './assets/texture-cat-plane.usda',
    preferredMaterialType: 'usdpreviewsurface',
    image: './assets/previews/asset-resolver.jpg',
    href: './asset-resolver.html'
  },
  {
    id: 'composition',
    title: 'USD Composition',
    subtitle: 'Composes sublayers and renders two Suzanne assets from one root layer.',
    defaultAsset: './assets/usd-composite-sample.usda',
    preferredMaterialType: 'auto',
    useComposition: true,
    image: './assets/previews/composition.jpg',
    href: './composition.html'
  },
  {
    id: 'export',
    title: 'USD Export',
    subtitle: 'Loads USD and exports the current native scene as USDA, USDC, or USDZ.',
    defaultAsset: './assets/suzanne-pbr.usda',
    preferredMaterialType: 'auto',
    useLayerExport: true,
    enableExport: true,
    image: './assets/previews/export.jpg',
    href: './export.html'
  }
];

export const DEMO_BY_ID = Object.fromEntries(DEMOS.map((demo) => [demo.id, demo]));
