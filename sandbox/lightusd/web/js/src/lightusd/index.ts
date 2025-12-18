// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Main entry point
//
// This module provides:
// 1. Core WASM module (createLightUSD)
// 2. Progressive loading API (ProgressiveScene, loadUSDProgressive)
// 3. Low-level worker bridge (WorkerBridge)
//
// Usage:
//   // High-level API (recommended)
//   import { loadUSDProgressive } from '@lightusd/web';
//
//   const scene = await loadUSDProgressive('model.usdz', {
//       autoLoad: true,
//       onPrimReady: (prim) => {
//           threeScene.add(prim.toThreeMesh());
//       }
//   });
//
//   // Low-level API
//   import createLightUSD from '@lightusd/web';
//
//   const module = await createLightUSD();
//   const result = module.loadUsdz(data);

// Core WASM module
export { default as createLightUSD } from './lightusd';
export type { LightUSDModule } from './lightusd';

// Core types
export type {
    Token,
    TokenConstructor,
    Path,
    PathConstructor,
    Value,
    ValueConstructor,
    Attribute,
    AttributeClass,
    Prim,
    PrimClass,
    Stage,
    StageClass,
    UsdaReaderResult,
    UsdaReaderResultClass,
} from './lightusd';

// Render types
export type {
    RenderMesh,
    RenderMeshClass,
    RenderTexture,
    RenderMaterial,
    RenderScene,
    RenderSceneClass,
    RenderConverter,
    RenderConverterConstructor,
} from './lightusd';

// USDZ types
export type {
    UsdzArchive,
    UsdzArchiveConstructor,
    UsdzLoaderResult,
    UsdzLoaderResultClass,
    UsdzRenderResult,
    UsdzRenderResultClass,
} from './lightusd';

// Progressive loading (high-level API)
export {
    ProgressiveScene,
    PrimProxy,
    loadUSDProgressive,
} from './ProgressiveScene';

export type {
    PrimState,
    LoadPriority,
    SceneState,
    PrimSkeleton,
    AssetRequest,
    LoadOptions,
    FrustumInfo,
} from './ProgressiveScene';

// Worker bridge (low-level API)
export { WorkerBridge } from './WorkerBridge';

export type {
    PrimSkeletonData,
    PrimGeometryData,
    AssetRequestData,
    ProcessQueueResult,
    ParseStructureResult,
    LoaderState,
    PrimLoadState,
    WorkerBridgeConfig,
    WorkerBridgeEvents,
    LoadPriority as WorkerLoadPriority,
} from './WorkerBridge';

// Default export is the module loader
export { default } from './lightusd';
