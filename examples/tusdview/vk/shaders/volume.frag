#version 450

// UsdVol volume fragment shader. Emission/absorption raymarch of a dense
// density grid (3D texture) in the volume's object space. Outputs premultiplied
// color (composited "over" the surface via VK_BLEND_FACTOR_ONE /
// ONE_MINUS_SRC_ALPHA).

layout(location = 0) in vec3 vWorld;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0, std140) uniform VolumeUBO {
  mat4 vp;
  mat4 model;
  mat4 invModel;
  vec4 camPos;
  vec4 bmin;
  vec4 bmax;
  vec4 albedo;    // .xyz albedo, .w densityScale
  vec4 emission;  // .xyz emission, .w background
} u;

layout(set = 0, binding = 1) uniform sampler3D uDensity;

bool rayAABB(vec3 o, vec3 d, vec3 lo, vec3 hi, out float t0, out float t1) {
  vec3 inv = 1.0 / d;
  vec3 ta = (lo - o) * inv;
  vec3 tb = (hi - o) * inv;
  vec3 tmin = min(ta, tb);
  vec3 tmax = max(ta, tb);
  t0 = max(max(tmin.x, tmin.y), tmin.z);
  t1 = min(min(tmax.x, tmax.y), tmax.z);
  return t1 > max(t0, 0.0);
}

void main() {
  vec3 bmin = u.bmin.xyz;
  vec3 bmax = u.bmax.xyz;
  float densityScale = u.albedo.w;
  float background = u.emission.w;

  // World ray -> object space.
  vec3 oo = (u.invModel * vec4(u.camPos.xyz, 1.0)).xyz;
  vec3 od = normalize((u.invModel * vec4(vWorld, 1.0)).xyz - oo);

  float t0, t1;
  if (!rayAABB(oo, od, bmin, bmax, t0, t1)) discard;
  t0 = max(t0, 0.0);

  vec3 ext = bmax - bmin;
  float step = min(ext.x, min(ext.y, ext.z)) / 128.0;
  if (step <= 0.0) step = (t1 - t0) / 256.0;

  float T = 1.0;
  vec3 L = vec3(0.0);
  float t = t0;
  for (int i = 0; i < 256; i++) {
    if (t >= t1) break;
    vec3 p = oo + od * (t + 0.5 * step);
    vec3 uvw = (p - bmin) / ext;
    float dens = (texture(uDensity, uvw).r - background) * densityScale;
    if (dens > 0.0) {
      float a = 1.0 - exp(-dens * step);
      vec3 src = u.albedo.xyz * a + u.emission.xyz * (dens * step);
      L += T * src;
      T *= (1.0 - a);
      if (T < 0.003) break;
    }
    t += step;
  }
  float alpha = 1.0 - T;
  if (alpha <= 0.001) discard;
  fragColor = vec4(L, alpha);  // premultiplied
}
