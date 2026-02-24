// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Schema Registry implementation

#include "lightusd/schema_registry.hh"
#include "lightusd/prim.hh"
#include "lightusd/stage.hh"

#include <algorithm>

namespace lightusd {
namespace v1 {

// ============================================================================
// Built-in Schema JSON Definitions
// ============================================================================

namespace builtin_schemas {

const char* const kMeshSchema = R"JSON({
  "typeName": "Mesh",
  "doc": "A polygonal mesh primitive",
  "inherits": "Gprim",
  "properties": [
    {"name": "points", "type": "point3f[]", "required": "required", "doc": "Vertex positions"},
    {"name": "faceVertexCounts", "type": "int[]", "required": "required", "doc": "Number of vertices per face"},
    {"name": "faceVertexIndices", "type": "int[]", "required": "required", "doc": "Vertex indices for each face"},
    {"name": "normals", "type": "normal3f[]", "required": "optional", "doc": "Surface normals", "interpolation": "faceVarying"},
    {"name": "primvars:st", "type": "texCoord2f[]", "required": "optional", "doc": "Texture coordinates", "interpolation": "faceVarying"},
    {"name": "subdivisionScheme", "type": "token", "required": "optional", "allowedValues": ["none", "catmullClark", "loop", "bilinear"]},
    {"name": "interpolateBoundary", "type": "token", "required": "optional", "allowedValues": ["none", "edgeOnly", "edgeAndCorner"]},
    {"name": "faceVaryingLinearInterpolation", "type": "token", "required": "optional"},
    {"name": "holeIndices", "type": "int[]", "required": "optional", "doc": "Face indices to treat as holes"},
    {"name": "cornerIndices", "type": "int[]", "required": "optional"},
    {"name": "cornerSharpnesses", "type": "float[]", "required": "optional"},
    {"name": "creaseIndices", "type": "int[]", "required": "optional"},
    {"name": "creaseLengths", "type": "int[]", "required": "optional"},
    {"name": "creaseSharpnesses", "type": "float[]", "required": "optional"}
  ],
  "required": ["points", "faceVertexCounts", "faceVertexIndices"],
  "apiSchemas": ["MaterialBindingAPI"]
)JSON";

const char* const kXformSchema = R"JSON({
  "typeName": "Xform",
  "doc": "A transformable group primitive",
  "inherits": "Imageable",
  "properties": [
    {"name": "xformOpOrder", "type": "token[]", "required": "optional", "doc": "Order of transform operations"},
    {"name": "xformOp:translate", "type": "double3", "required": "optional"},
    {"name": "xformOp:rotateXYZ", "type": "float3", "required": "optional"},
    {"name": "xformOp:rotateX", "type": "float", "required": "optional"},
    {"name": "xformOp:rotateY", "type": "float", "required": "optional"},
    {"name": "xformOp:rotateZ", "type": "float", "required": "optional"},
    {"name": "xformOp:scale", "type": "float3", "required": "optional"},
    {"name": "xformOp:orient", "type": "quatf", "required": "optional"},
    {"name": "xformOp:transform", "type": "matrix4d", "required": "optional"}
  ],
  "allowAdditionalProperties": true
)JSON";

const char* const kScopeSchema = R"JSON({
  "typeName": "Scope",
  "doc": "A grouping primitive with no transformation",
  "inherits": "Imageable",
  "properties": [],
  "allowAdditionalProperties": true
)JSON";

const char* const kMaterialSchema = R"JSON({
  "typeName": "Material",
  "doc": "A material definition containing shaders",
  "properties": [
    {"name": "outputs:surface", "type": "token", "required": "recommended", "doc": "Surface shader output"},
    {"name": "outputs:displacement", "type": "token", "required": "optional", "doc": "Displacement shader output"},
    {"name": "outputs:volume", "type": "token", "required": "optional", "doc": "Volume shader output"}
  ],
  "allowAdditionalProperties": true
)JSON";

const char* const kShaderSchema = R"JSON({
  "typeName": "Shader",
  "doc": "A shader node",
  "properties": [
    {"name": "info:id", "type": "token", "required": "required", "doc": "Shader type identifier"},
    {"name": "outputs:surface", "type": "token", "required": "optional"},
    {"name": "outputs:displacement", "type": "token", "required": "optional"}
  ],
  "allowAdditionalProperties": true
)JSON";

const char* const kNodeGraphSchema = R"JSON({
  "typeName": "NodeGraph",
  "doc": "A container for organizing shader nodes and connections in a network",
  "properties": [
    {"name": "nodedef", "type": "string", "required": "optional", "doc": "Reference to a MaterialX NodeDef"},
    {"name": "nodegraph_type", "type": "string", "required": "optional", "doc": "Type of the node graph (MaterialX)"}
  ],
  "allowAdditionalProperties": true
})JSON";

const char* const kMaterialXConfigAPISchema = R"JSON({
  "typeName": "MaterialXConfigAPI",
  "doc": "API schema providing MaterialX environment configuration on Material prims",
  "properties": [
    {"name": "config:mtlx:version", "type": "string", "required": "optional", "doc": "MaterialX version (default: 1.38)"},
    {"name": "config:mtlx:namespace", "type": "string", "required": "optional", "doc": "MaterialX namespace"},
    {"name": "config:mtlx:colorspace", "type": "string", "required": "optional", "doc": "Default colorspace (default: lin_rec709)"},
    {"name": "config:mtlx:sourceUri", "type": "string", "required": "optional", "doc": "Source URI for MaterialX documents"}
  ],
  "allowAdditionalProperties": true
})JSON";

const char* const kCameraSchema = R"JSON({
  "typeName": "Camera",
  "doc": "A camera primitive",
  "inherits": "Xform",
  "properties": [
    {"name": "projection", "type": "token", "required": "optional", "allowedValues": ["perspective", "orthographic"]},
    {"name": "focalLength", "type": "float", "required": "optional", "doc": "Focal length in mm", "minValue": 0.001},
    {"name": "horizontalAperture", "type": "float", "required": "optional", "doc": "Horizontal aperture in mm"},
    {"name": "verticalAperture", "type": "float", "required": "optional", "doc": "Vertical aperture in mm"},
    {"name": "horizontalApertureOffset", "type": "float", "required": "optional"},
    {"name": "verticalApertureOffset", "type": "float", "required": "optional"},
    {"name": "clippingRange", "type": "float2", "required": "optional", "doc": "Near and far clipping planes"},
    {"name": "clippingPlanes", "type": "float4[]", "required": "optional"},
    {"name": "fStop", "type": "float", "required": "optional", "minValue": 0},
    {"name": "focusDistance", "type": "float", "required": "optional", "minValue": 0},
    {"name": "shutterOpen", "type": "double", "required": "optional"},
    {"name": "shutterClose", "type": "double", "required": "optional"}
  ]
)JSON";

const char* const kSphereLightSchema = R"JSON({
  "typeName": "SphereLight",
  "doc": "A spherical area light",
  "inherits": "Light",
  "properties": [
    {"name": "radius", "type": "float", "required": "optional", "doc": "Radius of the sphere", "minValue": 0},
    {"name": "intensity", "type": "float", "required": "optional", "minValue": 0},
    {"name": "color", "type": "color3f", "required": "optional"},
    {"name": "exposure", "type": "float", "required": "optional"},
    {"name": "enableColorTemperature", "type": "bool", "required": "optional"},
    {"name": "colorTemperature", "type": "float", "required": "optional", "minValue": 1000, "maxValue": 40000},
    {"name": "normalize", "type": "bool", "required": "optional"},
    {"name": "diffuse", "type": "float", "required": "optional", "minValue": 0, "maxValue": 1},
    {"name": "specular", "type": "float", "required": "optional", "minValue": 0, "maxValue": 1},
    {"name": "treatAsPoint", "type": "bool", "required": "optional"}
  ]
)JSON";

const char* const kDistantLightSchema = R"JSON({
  "typeName": "DistantLight",
  "doc": "A distant/directional light",
  "inherits": "Light",
  "properties": [
    {"name": "angle", "type": "float", "required": "optional", "doc": "Angular size in degrees", "minValue": 0, "maxValue": 180},
    {"name": "intensity", "type": "float", "required": "optional", "minValue": 0},
    {"name": "color", "type": "color3f", "required": "optional"},
    {"name": "exposure", "type": "float", "required": "optional"},
    {"name": "enableColorTemperature", "type": "bool", "required": "optional"},
    {"name": "colorTemperature", "type": "float", "required": "optional"},
    {"name": "normalize", "type": "bool", "required": "optional"},
    {"name": "diffuse", "type": "float", "required": "optional"},
    {"name": "specular", "type": "float", "required": "optional"}
  ]
)JSON";

const char* const kDomeLightSchema = R"JSON({
  "typeName": "DomeLight",
  "doc": "An environment dome light",
  "inherits": "Light",
  "properties": [
    {"name": "texture:file", "type": "asset", "required": "optional", "doc": "Environment map texture"},
    {"name": "texture:format", "type": "token", "required": "optional", "allowedValues": ["automatic", "latlong", "mirroredBall", "angular", "cubeMapVerticalCross"]},
    {"name": "intensity", "type": "float", "required": "optional", "minValue": 0},
    {"name": "color", "type": "color3f", "required": "optional"},
    {"name": "exposure", "type": "float", "required": "optional"},
    {"name": "enableColorTemperature", "type": "bool", "required": "optional"},
    {"name": "colorTemperature", "type": "float", "required": "optional"},
    {"name": "normalize", "type": "bool", "required": "optional"},
    {"name": "diffuse", "type": "float", "required": "optional"},
    {"name": "specular", "type": "float", "required": "optional"},
    {"name": "guideRadius", "type": "float", "required": "optional"}
  ]
)JSON";

const char* const kSkelRootSchema = R"JSON({
  "typeName": "SkelRoot",
  "doc": "Root primitive for skeletal animation hierarchy",
  "inherits": "Boundable",
  "properties": [
    {"name": "skel:skeleton", "type": "token", "required": "optional", "doc": "Path to skeleton"}
  ],
  "allowAdditionalProperties": true
)JSON";

const char* const kSkeletonSchema = R"JSON({
  "typeName": "Skeleton",
  "doc": "A skeleton definition for skeletal animation",
  "inherits": "Boundable",
  "properties": [
    {"name": "joints", "type": "token[]", "required": "required", "doc": "Ordered list of joint names"},
    {"name": "jointNames", "type": "token[]", "required": "optional", "doc": "Display names for joints"},
    {"name": "bindTransforms", "type": "matrix4d[]", "required": "optional", "doc": "World-space bind transforms"},
    {"name": "restTransforms", "type": "matrix4d[]", "required": "optional", "doc": "Local-space rest transforms"}
  ],
  "required": ["joints"]
})JSON";

const char* const kSkelAnimationSchema = R"JSON({
  "typeName": "SkelAnimation",
  "doc": "Animation data for skeletal animation",
  "properties": [
    {"name": "joints", "type": "token[]", "required": "required", "doc": "Joint paths this animation targets"},
    {"name": "translations", "type": "float3[]", "required": "optional", "doc": "Joint local translation values"},
    {"name": "rotations", "type": "quatf[]", "required": "optional", "doc": "Joint local rotation values as quaternions"},
    {"name": "scales", "type": "float3[]", "required": "optional", "doc": "Joint local scale values"},
    {"name": "blendShapes", "type": "token[]", "required": "optional", "doc": "Blend shape targets to animate"},
    {"name": "blendShapeWeights", "type": "float[]", "required": "optional", "doc": "Blend shape weight values"}
  ],
  "required": ["joints"],
  "allowAdditionalProperties": true
})JSON";

const char* const kBlendShapeSchema = R"JSON({
  "typeName": "BlendShape",
  "doc": "A blend shape deformation target",
  "properties": [
    {"name": "offsets", "type": "vector3f[]", "required": "required", "doc": "Per-point position offsets"},
    {"name": "normalOffsets", "type": "vector3f[]", "required": "optional", "doc": "Per-point normal offsets"},
    {"name": "pointIndices", "type": "int[]", "required": "optional", "doc": "Indices of affected points for sparse representation"}
  ],
  "required": ["offsets"],
  "allowAdditionalProperties": true
})JSON";

const char* const kPointsSchema = R"JSON({
  "typeName": "Points",
  "doc": "A point cloud primitive",
  "inherits": "Gprim",
  "properties": [
    {"name": "points", "type": "point3f[]", "required": "required", "doc": "Point positions"},
    {"name": "widths", "type": "float[]", "required": "optional", "doc": "Point widths/sizes", "interpolation": "vertex"},
    {"name": "ids", "type": "int64[]", "required": "optional", "doc": "Stable point identifiers"},
    {"name": "velocities", "type": "vector3f[]", "required": "optional", "doc": "Point velocities"},
    {"name": "accelerations", "type": "vector3f[]", "required": "optional"},
    {"name": "normals", "type": "normal3f[]", "required": "optional"}
  ],
  "required": ["points"],
  "apiSchemas": ["MaterialBindingAPI"]
)JSON";

const char* const kBasisCurvesSchema = R"JSON({
  "typeName": "BasisCurves",
  "doc": "Curves primitive (hair, fur, etc.)",
  "inherits": "Gprim",
  "properties": [
    {"name": "points", "type": "point3f[]", "required": "required", "doc": "Control points"},
    {"name": "curveVertexCounts", "type": "int[]", "required": "required", "doc": "Vertices per curve"},
    {"name": "type", "type": "token", "required": "optional", "allowedValues": ["linear", "cubic"]},
    {"name": "basis", "type": "token", "required": "optional", "allowedValues": ["bezier", "bspline", "catmullRom"]},
    {"name": "wrap", "type": "token", "required": "optional", "allowedValues": ["nonperiodic", "periodic", "pinned"]},
    {"name": "widths", "type": "float[]", "required": "optional", "interpolation": "varying"},
    {"name": "normals", "type": "normal3f[]", "required": "optional"},
    {"name": "velocities", "type": "vector3f[]", "required": "optional"},
    {"name": "accelerations", "type": "vector3f[]", "required": "optional"}
  ],
  "required": ["points", "curveVertexCounts"],
  "apiSchemas": ["MaterialBindingAPI"]
)JSON";

const char* const kCubeSchema = R"JSON({
  "typeName": "Cube",
  "doc": "A cube primitive",
  "inherits": "Gprim",
  "properties": [
    {"name": "size", "type": "double", "required": "optional", "doc": "Side length", "minValue": 0}
  ],
  "apiSchemas": ["MaterialBindingAPI"]
)JSON";

const char* const kSphereSchema = R"JSON({
  "typeName": "Sphere",
  "doc": "A sphere primitive",
  "inherits": "Gprim",
  "properties": [
    {"name": "radius", "type": "double", "required": "optional", "doc": "Sphere radius", "minValue": 0}
  ],
  "apiSchemas": ["MaterialBindingAPI"]
)JSON";

const char* const kCylinderSchema = R"JSON({
  "typeName": "Cylinder",
  "doc": "A cylinder primitive",
  "inherits": "Gprim",
  "properties": [
    {"name": "radius", "type": "double", "required": "optional", "doc": "Cylinder radius", "minValue": 0},
    {"name": "height", "type": "double", "required": "optional", "doc": "Cylinder height", "minValue": 0},
    {"name": "axis", "type": "token", "required": "optional", "allowedValues": ["X", "Y", "Z"]}
  ],
  "apiSchemas": ["MaterialBindingAPI"]
)JSON";

const char* const kConeSchema = R"JSON({
  "typeName": "Cone",
  "doc": "A cone primitive",
  "inherits": "Gprim",
  "properties": [
    {"name": "radius", "type": "double", "required": "optional", "doc": "Base radius", "minValue": 0},
    {"name": "height", "type": "double", "required": "optional", "doc": "Cone height", "minValue": 0},
    {"name": "axis", "type": "token", "required": "optional", "allowedValues": ["X", "Y", "Z"]}
  ],
  "apiSchemas": ["MaterialBindingAPI"]
)JSON";

const char* const kCapsuleSchema = R"JSON({
  "typeName": "Capsule",
  "doc": "A capsule primitive (cylinder with hemispherical caps)",
  "inherits": "Gprim",
  "properties": [
    {"name": "radius", "type": "double", "required": "optional", "doc": "Capsule radius", "minValue": 0},
    {"name": "height", "type": "double", "required": "optional", "doc": "Cylinder portion height", "minValue": 0},
    {"name": "axis", "type": "token", "required": "optional", "allowedValues": ["X", "Y", "Z"]}
  ],
  "apiSchemas": ["MaterialBindingAPI"]
)JSON";

const char* const kRectLightSchema = R"JSON({
  "typeName": "RectLight",
  "doc": "A rectangular area light",
  "inherits": "Light",
  "properties": [
    {"name": "width", "type": "float", "required": "optional", "doc": "Width of the rectangle", "minValue": 0},
    {"name": "height", "type": "float", "required": "optional", "doc": "Height of the rectangle", "minValue": 0},
    {"name": "texture:file", "type": "asset", "required": "optional", "doc": "Texture to project from the light"}
  ]
})JSON";

const char* const kCylinderLightSchema = R"JSON({
  "typeName": "CylinderLight",
  "doc": "A cylindrical area light",
  "inherits": "Light",
  "properties": [
    {"name": "radius", "type": "float", "required": "optional", "doc": "Radius of the cylinder", "minValue": 0},
    {"name": "length", "type": "float", "required": "optional", "doc": "Length of the cylinder", "minValue": 0},
    {"name": "treatAsLine", "type": "bool", "required": "optional", "doc": "Treat as a line light for efficiency"}
  ]
})JSON";

const char* const kDiskLightSchema = R"JSON({
  "typeName": "DiskLight",
  "doc": "A disk-shaped area light",
  "inherits": "Light",
  "properties": [
    {"name": "radius", "type": "float", "required": "optional", "doc": "Radius of the disk", "minValue": 0}
  ]
})JSON";

const char* const kPortalLightSchema = R"JSON({
  "typeName": "PortalLight",
  "doc": "A portal light for guiding dome light importance sampling",
  "inherits": "Light",
  "properties": [
    {"name": "width", "type": "float", "required": "optional", "doc": "Width of the portal", "minValue": 0},
    {"name": "height", "type": "float", "required": "optional", "doc": "Height of the portal", "minValue": 0}
  ]
})JSON";

const char* const kMeshLightSchema = R"JSON({
  "typeName": "MeshLight",
  "doc": "A mesh-shaped area light (geometry from child mesh)",
  "inherits": "Light",
  "properties": []
})JSON";

const char* const kCollectionAPISchema = R"JSON({
  "typeName": "CollectionAPI",
  "doc": "API schema for defining collections of prims",
  "properties": [
    {"name": "expansionRule", "type": "token", "required": "optional", "allowedValues": ["expandPrims", "explicitOnly", "expandPrimsAndProperties"]},
    {"name": "includeRoot", "type": "bool", "required": "optional", "doc": "Whether to include the root prim in the collection"}
  ],
  "allowAdditionalProperties": true
})JSON";

const char* const kSkelBindingAPISchema = R"JSON({
  "typeName": "SkelBindingAPI",
  "doc": "API schema for binding skeletons to meshes",
  "properties": [
    {"name": "skel:skeleton", "type": "token", "required": "optional", "doc": "Path to Skeleton prim"},
    {"name": "skel:animationSource", "type": "token", "required": "optional", "doc": "Path to SkelAnimation prim"},
    {"name": "primvars:skel:jointIndices", "type": "int[]", "required": "optional", "doc": "Joint indices for each vertex"},
    {"name": "primvars:skel:jointWeights", "type": "float[]", "required": "optional", "doc": "Joint weights for each vertex"},
    {"name": "primvars:skel:geomBindTransform", "type": "matrix4d", "required": "optional", "doc": "Geometry bind-time transform"}
  ],
  "allowAdditionalProperties": true
})JSON";

// Base schemas (abstract, for inheritance)
const char* const kGprimSchema = R"JSON({
  "typeName": "Gprim",
  "doc": "Base class for geometric primitives",
  "inherits": "Boundable",
  "properties": [
    {"name": "doubleSided", "type": "bool", "required": "optional", "doc": "Render both sides"},
    {"name": "orientation", "type": "token", "required": "optional", "allowedValues": ["rightHanded", "leftHanded"]},
    {"name": "primvars:displayColor", "type": "color3f[]", "required": "optional", "interpolation": "constant"},
    {"name": "primvars:displayOpacity", "type": "float[]", "required": "optional", "interpolation": "constant"}
  ],
  "apiSchemas": ["MaterialBindingAPI"]
)JSON";

const char* const kBoundableSchema = R"JSON({
  "typeName": "Boundable",
  "doc": "Base class for primitives with bounds",
  "inherits": "Imageable",
  "properties": [
    {"name": "extent", "type": "float3[]", "required": "optional", "doc": "Bounding box [min, max]", "minArraySize": 2, "maxArraySize": 2}
  ]
)JSON";

const char* const kImageableSchema = R"JSON({
  "typeName": "Imageable",
  "doc": "Base class for renderable primitives",
  "properties": [
    {"name": "visibility", "type": "token", "required": "optional", "allowedValues": ["inherited", "invisible"]},
    {"name": "purpose", "type": "token", "required": "optional", "allowedValues": ["default", "render", "proxy", "guide"]},
    {"name": "proxyPrim", "type": "token", "required": "optional"}
  ]
)JSON";

const char* const kLightSchema = R"JSON({
  "typeName": "Light",
  "doc": "Base class for lights",
  "inherits": "Xform",
  "properties": [
    {"name": "intensity", "type": "float", "required": "optional", "minValue": 0},
    {"name": "color", "type": "color3f", "required": "optional"},
    {"name": "exposure", "type": "float", "required": "optional"},
    {"name": "enableColorTemperature", "type": "bool", "required": "optional"},
    {"name": "colorTemperature", "type": "float", "required": "optional"},
    {"name": "normalize", "type": "bool", "required": "optional"},
    {"name": "diffuse", "type": "float", "required": "optional"},
    {"name": "specular", "type": "float", "required": "optional"}
  ]
)JSON";

} // namespace builtin_schemas

// ============================================================================
// SchemaRegistry implementation
// ============================================================================

SchemaRegistry& SchemaRegistry::instance() {
    static SchemaRegistry instance;
    return instance;
}

SchemaRegistry::SchemaRegistry() {
    load_builtin_schemas();
}

void SchemaRegistry::load_builtin_schemas() {
    if (builtins_loaded_) return;

    // Load base schemas first (for inheritance)
    register_schema(builtin_schemas::kImageableSchema, "builtin");
    register_schema(builtin_schemas::kBoundableSchema, "builtin");
    register_schema(builtin_schemas::kGprimSchema, "builtin");
    register_schema(builtin_schemas::kLightSchema, "builtin");

    // Load concrete schemas
    register_schema(builtin_schemas::kMeshSchema, "builtin");
    register_schema(builtin_schemas::kXformSchema, "builtin");
    register_schema(builtin_schemas::kScopeSchema, "builtin");
    register_schema(builtin_schemas::kMaterialSchema, "builtin");
    register_schema(builtin_schemas::kShaderSchema, "builtin");
    register_schema(builtin_schemas::kNodeGraphSchema, "builtin");
    register_schema(builtin_schemas::kMaterialXConfigAPISchema, "builtin");
    register_schema(builtin_schemas::kCameraSchema, "builtin");
    register_schema(builtin_schemas::kSphereLightSchema, "builtin");
    register_schema(builtin_schemas::kDistantLightSchema, "builtin");
    register_schema(builtin_schemas::kDomeLightSchema, "builtin");
    register_schema(builtin_schemas::kRectLightSchema, "builtin");
    register_schema(builtin_schemas::kCylinderLightSchema, "builtin");
    register_schema(builtin_schemas::kDiskLightSchema, "builtin");
    register_schema(builtin_schemas::kPortalLightSchema, "builtin");
    register_schema(builtin_schemas::kMeshLightSchema, "builtin");
    register_schema(builtin_schemas::kCollectionAPISchema, "builtin");
    register_schema(builtin_schemas::kSkelBindingAPISchema, "builtin");
    register_schema(builtin_schemas::kSkelRootSchema, "builtin");
    register_schema(builtin_schemas::kSkeletonSchema, "builtin");
    register_schema(builtin_schemas::kSkelAnimationSchema, "builtin");
    register_schema(builtin_schemas::kBlendShapeSchema, "builtin");
    register_schema(builtin_schemas::kPointsSchema, "builtin");
    register_schema(builtin_schemas::kBasisCurvesSchema, "builtin");
    register_schema(builtin_schemas::kCubeSchema, "builtin");
    register_schema(builtin_schemas::kSphereSchema, "builtin");
    register_schema(builtin_schemas::kCylinderSchema, "builtin");
    register_schema(builtin_schemas::kConeSchema, "builtin");
    register_schema(builtin_schemas::kCapsuleSchema, "builtin");

    builtins_loaded_ = true;
}

Result<void> SchemaRegistry::register_schema(std::string_view json, const std::string& source) {
    auto schema_result = PrimSchema::from_json(json);
    if (!schema_result) {
        return make_error("Failed to parse schema: " + schema_result.error().message);
    }

    PrimSchema schema = std::move(schema_result).value();
    schema.source = source.empty() ? "user" : source;

    return register_schema(std::move(schema));
}

Result<void> SchemaRegistry::register_schema(PrimSchema schema) {
    if (schema.type_name.empty()) {
        return make_error("Schema must have a type name");
    }

    // Resolve inheritance if parent exists
    if (!schema.inherits_from.empty()) {
        auto resolved = resolve_inheritance(schema);
        if (!resolved) {
            // Parent not found yet, register anyway (will be resolved later)
        }
    }

    schemas_[schema.type_name] = std::move(schema);
    return {};
}

Result<void> SchemaRegistry::register_schemas(std::string_view json_array, const std::string& source) {
    auto parsed = JsonValue::parse(json_array);
    if (!parsed) {
        return make_error("Failed to parse JSON: " + parsed.error().message);
    }

    if (!parsed.value().is_array()) {
        return make_error("Expected JSON array of schemas");
    }

    for (const auto& item : parsed.value().as_array()) {
        auto result = register_schema(item.to_string(), source);
        if (!result) {
            return result;
        }
    }

    return {};
}

bool SchemaRegistry::unregister_schema(const std::string& type_name) {
    return schemas_.erase(type_name) > 0;
}

void SchemaRegistry::clear() {
    schemas_.clear();
    builtins_loaded_ = false;
}

void SchemaRegistry::reset_to_defaults() {
    clear();
    load_builtin_schemas();
}

const PrimSchema* SchemaRegistry::get_schema(std::string_view type_name) const {
    auto it = schemas_.find(std::string(type_name));
    return it != schemas_.end() ? &it->second : nullptr;
}

bool SchemaRegistry::has_schema(std::string_view type_name) const {
    return schemas_.find(std::string(type_name)) != schemas_.end();
}

std::vector<std::string> SchemaRegistry::registered_types() const {
    std::vector<std::string> types;
    types.reserve(schemas_.size());
    for (const auto& [name, _] : schemas_) {
        types.push_back(name);
    }
    return types;
}

ValidationResult SchemaRegistry::validate(const Prim& prim) const {
    const std::string& type_name = prim.type_name();

    if (type_name.empty()) {
        ValidationResult result;
        result.add_warning("", "Prim has no type name, skipping schema validation");
        return result;
    }

    const PrimSchema* schema = get_schema(type_name);
    if (!schema) {
        ValidationResult result;
        result.add_info("", "No schema registered for type '" + type_name + "'");
        return result;
    }

    return validate(prim, *schema);
}

ValidationResult SchemaRegistry::validate(const Prim& prim, const PrimSchema& schema) const {
    // Get resolved schema with inheritance
    auto resolved_result = get_resolved_schema(schema.type_name);
    if (resolved_result) {
        return resolved_result.value().validate(prim);
    }
    return schema.validate(prim);
}

std::map<std::string, ValidationResult> SchemaRegistry::validate_stage(const Stage& stage) const {
    std::map<std::string, ValidationResult> results;

    for (size_t i = 0; i < stage.root_prim_count(); ++i) {
        const Prim* root = stage.root_prim(i);
        if (!root) continue;

        auto tree_results = validate_prim_tree(*root);
        results.insert(tree_results.begin(), tree_results.end());
    }

    return results;
}

std::map<std::string, ValidationResult> SchemaRegistry::validate_prim_tree(const Prim& root) const {
    std::map<std::string, ValidationResult> results;

    // Validate this prim
    std::string path = root.path().full_path();
    results[path] = validate(root);

    // Recursively validate children
    for (size_t i = 0; i < root.child_count(); ++i) {
        const Prim* child = root.child(i);
        if (!child) continue;

        auto child_results = validate_prim_tree(*child);
        results.insert(child_results.begin(), child_results.end());
    }

    return results;
}

Result<PrimSchema> SchemaRegistry::get_resolved_schema(std::string_view type_name) const {
    const PrimSchema* schema = get_schema(type_name);
    if (!schema) {
        return make_error("Schema not found: " + std::string(type_name));
    }

    // If no inheritance, return copy
    if (schema->inherits_from.empty()) {
        return *schema;
    }

    // Build inheritance chain
    std::vector<const PrimSchema*> chain;
    const PrimSchema* current = schema;

    while (current) {
        chain.push_back(current);
        if (current->inherits_from.empty()) break;

        current = get_schema(current->inherits_from);
        if (!current) {
            // Parent not found, stop here
            break;
        }

        // Detect cycles
        for (size_t i = 0; i < chain.size() - 1; ++i) {
            if (chain[i] == current) {
                return make_error("Circular inheritance detected in schema " +
                                  std::string(type_name));
            }
        }
    }

    // Merge schemas from base to derived
    PrimSchema resolved = *chain.back();
    for (size_t i = chain.size() - 1; i > 0; --i) {
        resolved.merge_parent(*chain[i - 1]);
    }

    // The derived schema's type name should be preserved
    resolved.type_name = std::string(type_name);

    return resolved;
}

Result<void> SchemaRegistry::resolve_inheritance(PrimSchema& schema) const {
    if (schema.inherits_from.empty()) return {};

    const PrimSchema* parent = get_schema(schema.inherits_from);
    if (!parent) {
        return make_error("Parent schema not found: " + schema.inherits_from);
    }

    schema.merge_parent(*parent);
    return {};
}

const char* SchemaRegistry::get_builtin_schema_json(std::string_view type_name) {
    if (type_name == "Mesh") return builtin_schemas::kMeshSchema;
    if (type_name == "Xform") return builtin_schemas::kXformSchema;
    if (type_name == "Scope") return builtin_schemas::kScopeSchema;
    if (type_name == "Material") return builtin_schemas::kMaterialSchema;
    if (type_name == "Shader") return builtin_schemas::kShaderSchema;
    if (type_name == "NodeGraph") return builtin_schemas::kNodeGraphSchema;
    if (type_name == "MaterialXConfigAPI") return builtin_schemas::kMaterialXConfigAPISchema;
    if (type_name == "Camera") return builtin_schemas::kCameraSchema;
    if (type_name == "SphereLight") return builtin_schemas::kSphereLightSchema;
    if (type_name == "DistantLight") return builtin_schemas::kDistantLightSchema;
    if (type_name == "DomeLight") return builtin_schemas::kDomeLightSchema;
    if (type_name == "RectLight") return builtin_schemas::kRectLightSchema;
    if (type_name == "CylinderLight") return builtin_schemas::kCylinderLightSchema;
    if (type_name == "DiskLight") return builtin_schemas::kDiskLightSchema;
    if (type_name == "PortalLight") return builtin_schemas::kPortalLightSchema;
    if (type_name == "MeshLight") return builtin_schemas::kMeshLightSchema;
    if (type_name == "CollectionAPI") return builtin_schemas::kCollectionAPISchema;
    if (type_name == "SkelBindingAPI") return builtin_schemas::kSkelBindingAPISchema;
    if (type_name == "SkelRoot") return builtin_schemas::kSkelRootSchema;
    if (type_name == "Skeleton") return builtin_schemas::kSkeletonSchema;
    if (type_name == "SkelAnimation") return builtin_schemas::kSkelAnimationSchema;
    if (type_name == "BlendShape") return builtin_schemas::kBlendShapeSchema;
    if (type_name == "Points") return builtin_schemas::kPointsSchema;
    if (type_name == "BasisCurves") return builtin_schemas::kBasisCurvesSchema;
    if (type_name == "Cube") return builtin_schemas::kCubeSchema;
    if (type_name == "Sphere") return builtin_schemas::kSphereSchema;
    if (type_name == "Cylinder") return builtin_schemas::kCylinderSchema;
    if (type_name == "Cone") return builtin_schemas::kConeSchema;
    if (type_name == "Capsule") return builtin_schemas::kCapsuleSchema;
    if (type_name == "Gprim") return builtin_schemas::kGprimSchema;
    if (type_name == "Boundable") return builtin_schemas::kBoundableSchema;
    if (type_name == "Imageable") return builtin_schemas::kImageableSchema;
    if (type_name == "Light") return builtin_schemas::kLightSchema;
    return nullptr;
}

std::vector<std::string> SchemaRegistry::builtin_schema_types() {
    return {
        "Imageable", "Boundable", "Gprim", "Light",
        "Mesh", "Xform", "Scope", "Material", "Shader", "NodeGraph", "MaterialXConfigAPI", "Camera",
        "SphereLight", "DistantLight", "DomeLight",
        "RectLight", "CylinderLight", "DiskLight", "PortalLight", "MeshLight",
        "CollectionAPI", "SkelBindingAPI",
        "SkelRoot", "Skeleton", "SkelAnimation", "BlendShape",
        "Points", "BasisCurves",
        "Cube", "Sphere", "Cylinder", "Cone", "Capsule"
    };
}

} // namespace v1
} // namespace lightusd
