#version 450
#extension GL_GOOGLE_include_directive : require

// GPU-tessellation path, pre-tessellation vertex stage. Applies the blendshape
// morph + skinning to each control point (object space), then forwards it to the
// tess control shader. The tessellator subdivides the DEFORMED surface;
// displacement is applied per tess-vertex in mesh_tess.tese. Shares the deform
// math with mesh.vert (deform.glsl). Object-space output only.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in uvec4 aJoint;
layout(location = 4) in vec4 aWeight;
layout(location = 5) in uvec2 aInfluence;
layout(location = 8) in uvec2 aMorphOffsetCount;  // GPU morph (offset,count); 0 = none

// Blendshape morph + linear-blend skinning (sets 1,2,7,8,9 + applyMorphSkin).
#include "deform.glsl"

layout(location = 0) out vec3 vcPos;
layout(location = 1) out vec3 vcNrm;
layout(location = 2) out vec2 vcUV;

void main() {
  applyMorphSkin(aPos, aNormal, aMorphOffsetCount, aJoint, aWeight, aInfluence,
                 vcPos, vcNrm);
  vcUV = aUV;
}
