// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - TypeScript type definitions

export interface LightUSDModule {
    // Version
    version(): string;

    // USDA Reader
    readUsdaString(content: string): UsdaReaderResult;

    // Classes
    Token: TokenConstructor;
    Path: PathConstructor;
    Value: ValueConstructor;
    Attribute: AttributeClass;
    Prim: PrimClass;
    Stage: StageClass;
    UsdaReaderResult: UsdaReaderResultClass;

    // Render Data (WebGPU conversion)
    RenderConverter: RenderConverterConstructor;
    RenderScene: RenderSceneClass;
    RenderMesh: RenderMeshClass;
}

// RenderConverter constructor
export interface RenderConverterConstructor {
    new(): RenderConverter;
}

// RenderScene class
export interface RenderSceneClass {
    new(): RenderScene;
}

// RenderMesh class
export interface RenderMeshClass {
    new(): RenderMesh;
}

// Token
export interface TokenConstructor {
    new(): Token;
    new(str: string): Token;
}

export interface Token {
    str(): string;
    empty(): boolean;
    equals(other: Token): boolean;
    delete(): void;
}

// Path
export interface PathConstructor {
    new(): Path;
    new(pathStr: string): Path;
}

export interface Path {
    isValid(): boolean;
    isEmpty(): boolean;
    isAbsolute(): boolean;
    isRoot(): boolean;
    isPrimPath(): boolean;
    isPropertyPath(): boolean;
    primPart(): string;
    propPart(): string;
    fullPath(): string;
    elementName(): string;
    parent(): Path;
    appendChild(name: string): Path;
    appendProperty(name: string): Path;
    appendVariantSelection(variantSet: string, variantName: string): Path;
    hasVariantSelections(): boolean;
    stripVariantSelections(): Path;
    equals(other: Path): boolean;
    delete(): void;
}

// Value
export interface ValueConstructor {
    new(): Value;
    fromBool(v: boolean): Value;
    fromInt(v: number): Value;
    fromFloat(v: number): Value;
    fromDouble(v: number): Value;
    fromString(v: string): Value;
    fromFloat3(x: number, y: number, z: number): Value;
}

export interface Value {
    typeName(): string;
    isArray(): boolean;
    arraySize(): number;
    toJS(): any;
    delete(): void;
}

// Attribute
export interface AttributeClass {
    new(): Attribute;
}

export interface Attribute {
    name(): string;
    typeName(): string;
    hasValue(): boolean;
    hasTimeSamples(): boolean;
    value(): Value;
    valueAtTime(time: number): Value;
    timeSampleTimes(): number[];
    delete(): void;
}

// Prim
export interface PrimClass {
    new(): Prim;
}

export interface Prim {
    isValid(): boolean;
    name(): string;
    typeName(): string;
    path(): Path;
    hasChildren(): boolean;
    childCount(): number;
    childNames(): string[];
    child(name: string): Prim;
    childAt(index: number): Prim;
    hasProperties(): boolean;
    propertyCount(): number;
    propertyNames(): string[];
    attribute(name: string): Attribute;
    hasVariantSets(): boolean;
    variantSetCount(): number;
    variantSetNames(): string[];
    getVariantSelection(variantSetName: string): string;
    kind(): string;
    purpose(): string;
    isActive(): boolean;
    isInstanceable(): boolean;
    delete(): void;
}

// Stage
export interface StageClass {
    new(): Stage;
}

export interface Stage {
    isValid(): boolean;
    defaultPrim(): string;
    upAxis(): string;
    metersPerUnit(): number;
    timeCodesPerSecond(): number;
    framesPerSecond(): number;
    startTimeCode(): number;
    endTimeCode(): number;
    rootPrimCount(): number;
    rootPrimNames(): string[];
    rootPrim(index: number): Prim;
    rootPrimByName(name: string): Prim;
    primAtPath(path: string): Prim;
    toUsda(): string;
    delete(): void;
}

// USDA Reader Result
export interface UsdaReaderResultClass {
    new(): UsdaReaderResult;
}

export interface UsdaReaderResult {
    ok(): boolean;
    error(): string;
    stage(): Stage;
    delete(): void;
}

// RenderMesh
export interface RenderMesh {
    name(): string;
    path(): string;
    vertexCount(): number;
    triangleCount(): number;
    isValid(): boolean;
    doubleSided(): boolean;
    positions(): Float32Array;    // vec3 data
    normals(): Float32Array | null;      // vec3 data
    texcoords(): Float32Array | null;    // vec2 data
    tangents(): Float32Array | null;     // vec4 data
    indices(): Uint32Array;
    boundsMin(): number[];        // [x, y, z]
    boundsMax(): number[];        // [x, y, z]
    transform(): Float32Array;    // 4x4 matrix, 16 floats
    submeshCount(): number;
    submesh(index: number): { indexOffset: number; indexCount: number; materialIndex: number } | null;
    delete(): void;
}

// RenderTexture
export interface RenderTexture {
    name(): string;
    uri(): string;
    mimeType(): string;
    width(): number;
    height(): number;
    channels(): number;
    isHdr(): boolean;
    isValid(): boolean;
    fileData(): Uint8Array | null;     // Raw file bytes for browser decode
    dataU8(): Uint8Array | null;       // Decoded RGBA8 data (C++ decode)
    dataF32(): Float32Array | null;    // Decoded float data (HDR, C++ decode)
    webgpuFormat(): string;            // WebGPU format string
    delete(): void;
}

// RenderMaterial
export interface RenderMaterial {
    name(): string;
    path(): string;
    isValid(): boolean;
    baseColor(): number[];             // [r, g, b, a]
    baseColorTexture(): number;        // Index or -1
    metallic(): number;
    roughness(): number;
    metallicRoughnessTexture(): number;
    normalTexture(): number;
    normalScale(): number;
    emissive(): number[];              // [r, g, b]
    emissiveTexture(): number;
    occlusionTexture(): number;
    occlusionStrength(): number;
    doubleSided(): boolean;
    alphaCutoff(): number;
    delete(): void;
}

// RenderScene
export interface RenderScene {
    name(): string;
    upAxis(): string;
    metersPerUnit(): number;
    isValid(): boolean;
    meshCount(): number;
    materialCount(): number;
    textureCount(): number;
    cameraCount(): number;
    lightCount(): number;
    mesh(index: number): RenderMesh;
    material(index: number): RenderMaterial;
    texture(index: number): RenderTexture;
    boundsMin(): number[];
    boundsMax(): number[];
    defaultCamera(): number;
    delete(): void;
}

// RenderConverter
export interface RenderConverter {
    convert(stage: Stage, time?: number, triangulate?: boolean,
            computeNormals?: boolean, computeTangents?: boolean): RenderScene;
    error(): string;
    delete(): void;
}

// Module loader
declare function createLightUSDModule(): Promise<LightUSDModule>;
export default createLightUSDModule;
