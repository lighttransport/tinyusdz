/**
 * TinyUSDZ TypeScript Definitions
 *
 * TypeScript definitions for TinyUSDZ C API bindings.
 * Can be used with JavaScript via node-ffi, node-gyp, or native addons.
 */

// Result codes
export enum Result {
  SUCCESS = 0,
  FILE_NOT_FOUND = -1,
  PARSE_FAILED = -2,
  OUT_OF_MEMORY = -3,
  INVALID_ARGUMENT = -4,
  NOT_SUPPORTED = -5,
  COMPOSITION_FAILED = -6,
  INVALID_FORMAT = -7,
  IO_ERROR = -8,
  INTERNAL = -99,
}

// Format types
export enum Format {
  AUTO = 0,
  USDA = 1,
  USDC = 2,
  USDZ = 3,
}

// Prim types
export enum PrimType {
  UNKNOWN = 0,
  XFORM = 1,
  MESH = 2,
  MATERIAL = 3,
  SHADER = 4,
  CAMERA = 5,
  DISTANT_LIGHT = 6,
  SPHERE_LIGHT = 7,
  RECT_LIGHT = 8,
  DISK_LIGHT = 9,
  CYLINDER_LIGHT = 10,
  DOME_LIGHT = 11,
  SKELETON = 12,
  SKELROOT = 13,
  SKELANIMATION = 14,
  SCOPE = 15,
  GEOMSUBSET = 16,
  SPHERE = 17,
  CUBE = 18,
  CYLINDER = 19,
  CAPSULE = 20,
  CONE = 21,
}

// Value types
export enum ValueType {
  NONE = 0,
  BOOL = 1,
  INT = 2,
  UINT = 3,
  FLOAT = 5,
  DOUBLE = 6,
  STRING = 7,
  FLOAT2 = 13,
  FLOAT3 = 14,
  FLOAT4 = 15,
  DOUBLE2 = 16,
  DOUBLE3 = 17,
  DOUBLE4 = 18,
  MATRIX3D = 22,
  MATRIX4D = 23,
  QUATF = 24,
  QUATD = 25,
  COLOR3F = 26,
  NORMAL3F = 29,
  POINT3F = 31,
  TEXCOORD2F = 33,
  ARRAY = 41,
  TIME_SAMPLES = 43,
}

// Options for loading
export interface LoadOptions {
  maxMemoryLimitMb?: number;
  maxDepth?: number;
  enableComposition?: boolean;
  strictMode?: boolean;
  structureOnly?: boolean;
}

// Mesh data
export interface MeshData {
  points: Float32Array | null;
  indices: Int32Array | null;
  faceCounts: Int32Array | null;
  normals: Float32Array | null;
  uvs: Float32Array | null;
  vertexCount: number;
  faceCount: number;
  indexCount: number;
}

// Transform matrix
export interface Transform {
  matrix: Float64Array;  // 4x4 matrix
}

// Value wrapper
export class Value {
  readonly type: ValueType;
  readonly typeName: string;
  readonly isArray: boolean;
  readonly arraySize: number;

  getFloat(): number | null;
  getDouble(): number | null;
  getInt(): number | null;
  getBool(): boolean | null;
  getString(): string | null;
  getFloat2(): [number, number] | null;
  getFloat3(): [number, number, number] | null;
  getFloat4(): [number, number, number, number] | null;
  getMatrix4d(): Float64Array | null;
  isAnimated(): boolean;
  getTimeSamples(): { times: number[], count: number } | null;
}

// Prim wrapper
export class Prim {
  readonly name: string;
  readonly path: string;
  readonly type: PrimType;
  readonly typeName: string;
  readonly childCount: number;
  readonly propertyCount: number;

  isType(type: PrimType): boolean;
  isMesh(): boolean;
  isXform(): boolean;
  getChild(index: number): Prim | null;
  getChildren(): Prim[];
  getPropertyName(index: number): string;
  getProperty(name: string): Value | null;

  // Mesh operations
  getMeshData(): MeshData | null;
  getSubdivisionScheme(): string | null;

  // Transform operations
  getLocalMatrix(time?: number): Transform | null;
  getWorldMatrix(time?: number): Transform | null;

  // Material operations
  getBoundMaterial(): Prim | null;
  getSurfaceShader(): Prim | null;
  getShaderInput(name: string): Value | null;
  getShaderType(): string | null;
}

// Stage wrapper
export class Stage {
  readonly rootPrim: Prim | null;
  readonly hasAnimation: boolean;

  getPrimAtPath(path: string): Prim | null;
  getTimeRange(): { start: number, end: number, fps: number } | null;
  getMemoryStats(): { used: number, peak: number };
}

// Module interface
export interface TinyUSDZ {
  // Initialization
  init(): boolean;
  shutdown(): void;
  getVersion(): string;

  // Loading
  loadFromFile(filepath: string, options?: LoadOptions): Stage;
  loadFromMemory(data: Buffer | Uint8Array, format?: Format): Stage;
  detectFormat(filepath: string): Format;

  // Result/Type conversion
  resultToString(result: Result): string;
  primTypeToString(type: PrimType): string;
  valueTypeToString(type: ValueType): string;

  // Constants
  Result: typeof Result;
  Format: typeof Format;
  PrimType: typeof PrimType;
  ValueType: typeof ValueType;
}

declare const tinyusdz: TinyUSDZ;

export default tinyusdz;