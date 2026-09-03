#version 450

// UsdVol volume fragment shader. Emission/absorption raymarch of a dense
// density grid (3D texture) in the volume's object space. Outputs premultiplied
// color (composited "over" the surface via VK_BLEND_FACTOR_ONE /
// ONE_MINUS_SRC_ALPHA).

layout(location = 0) in vec3 vWorld;
#ifdef LUSDVIEW_OIT
layout(location = 0) out vec4 outAccum;
layout(location = 1) out float outReveal;
#else
layout(location = 0) out vec4 fragColor;
#endif

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

// R=density, G=emission/flame, B=temperature. bmin.w/bmax.w indicate
// whether the optional channels are present.
layout(set = 0, binding = 1) uniform sampler3D uFields;

vec3 blackbody(float value) {
  float k = value > 100.0 ? value : 1000.0 + 5500.0 * max(value, 0.0);
  float t = clamp(k / 100.0, 10.0, 400.0);
  float r = t <= 66.0 ? 1.0 : 1.2929362 * pow(t - 60.0, -0.13320476);
  float g = t <= 66.0 ? 0.39008158 * log(t) - 0.63184144
                      : 1.1298909 * pow(t - 60.0, -0.07551485);
  float b = t >= 66.0 ? 1.0 : (t <= 19.0 ? 0.0
                                         : 0.5432068 * log(t - 10.0) - 1.1962541);
  return clamp(vec3(r, g, b), 0.0, 1.0);
}

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
    vec3 fields = texture(uFields, uvw).rgb;
    float dens = (fields.r - background) * densityScale;
    if (dens > 0.0) {
      float a = 1.0 - exp(-dens * step);
      float ew = u.bmin.w > 0.5 ? max(fields.g, 0.0) : dens;
      vec3 ec = u.emission.xyz;
      if (u.bmax.w > 0.5) {
        float temp = max(fields.b, 0.0);
        vec3 tint = blackbody(temp);
        ec = dot(ec, vec3(1.0)) > 0.0 ? ec * tint : tint;
        ew = max(ew, temp);
      }
      vec3 src = u.albedo.xyz * a + ec * (ew * step);
      L += T * src;
      T *= (1.0 - a);
      if (T < 0.003) break;
    }
    t += step;
  }
  float alpha = 1.0 - T;
  if (alpha <= 0.001) discard;
#ifdef LUSDVIEW_OIT
  vec3 color = L / max(alpha, 1.0e-5);
  float weight = clamp(
      pow(min(1.0, alpha * 10.0) + 0.01, 3.0) * 1.0e8 *
          pow(1.0 - gl_FragCoord.z * 0.9, 3.0),
      1.0e-2, 3.0e3);
  outAccum = vec4(color * alpha, alpha) * weight;
  outReveal = alpha;
#else
  fragColor = vec4(L, alpha);  // premultiplied
#endif
}
