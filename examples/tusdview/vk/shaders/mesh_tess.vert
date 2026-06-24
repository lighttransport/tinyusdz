#version 450

// GPU-tessellation displacement path: pass object-space position/normal/uv
// straight through to the tessellation control shader. Skinning/morph are not
// supported here (displaced meshes that are skinned fall back to the coarse path).

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

layout(location = 0) out vec3 vcPos;
layout(location = 1) out vec3 vcNrm;
layout(location = 2) out vec2 vcUV;

void main() {
  vcPos = aPos;
  vcNrm = aNormal;
  vcUV = aUV;
}
