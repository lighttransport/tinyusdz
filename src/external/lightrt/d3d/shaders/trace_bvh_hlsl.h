// Auto-generated from trace_bvh.hlsl. Do not edit by hand.
#pragma once
static const char trace_bvh_hlsl[] = R"HLSLSRC(
// trace_bvh.hlsl — Direct3D 11 (SM5.0) compute BVH traversal for LightRT.
//
// DECOMPILED from vk/shaders/trace_bvh.spv (the committed Vulkan SPIR-V) with
// SPIRV-Cross (--hlsl --shader-model 50). It mirrors the scalar CPU kernel and
// the Vulkan trace_bvh compute shader bit-for-bit. Regenerate after editing the
// GLSL/SPIR-V via d3d/shaders/gen_trace_bvh_hlsl.sh.
//
// Bindings: t0 = BVH nodes, t1 = leaf blocks, t2 = rays, u3 = hits (RW),
// cbuffer PC = {root, node_count, block_count, ray_count}. Spec constants
// W (BVH width 4/8) and STACK are set via SPIRV_CROSS_CONSTANT_ID_0/_1 macros.
#ifndef SPIRV_CROSS_CONSTANT_ID_0
#define SPIRV_CROSS_CONSTANT_ID_0 8u
#endif
static const uint W = SPIRV_CROSS_CONSTANT_ID_0;
static const uint _164 = (8u * W);
static const uint _167 = (10u * W);
static const uint _170 = (9u * W);
#ifndef SPIRV_CROSS_CONSTANT_ID_1
#define SPIRV_CROSS_CONSTANT_ID_1 96u
#endif
static const uint STACK = SPIRV_CROSS_CONSTANT_ID_1;
static const uint _264 = (0u * W);
static const uint _274 = (1u * W);
static const uint _284 = (2u * W);
static const uint _294 = (3u * W);
static const uint _304 = (4u * W);
static const uint _314 = (5u * W);
static const uint _324 = (6u * W);
static const uint _334 = (7u * W);
static const uint _344 = (8u * W);
static const uint _542 = (7u * W);
static const uint _558 = (0u * W);
static const uint _568 = (1u * W);
static const uint _578 = (2u * W);
static const uint _588 = (3u * W);
static const uint _598 = (4u * W);
static const uint _608 = (5u * W);
static const uint _753 = (6u * W);
static const uint3 gl_WorkGroupSize = uint3(64u, 1u, 1u);

ByteAddressBuffer _49 : register(t2);
ByteAddressBuffer _249 : register(t1);
ByteAddressBuffer _540 : register(t0);
RWByteAddressBuffer _801 : register(u3);
cbuffer PC
{
    uint pc_root : packoffset(c0);
    uint pc_node_count : packoffset(c0.y);
    uint pc_block_count : packoffset(c0.z);
    uint pc_ray_count : packoffset(c0.w);
};


static uint3 gl_GlobalInvocationID;
struct SPIRV_Cross_Input
{
    uint3 gl_GlobalInvocationID : SV_DispatchThreadID;
};

float fu(uint i)
{
    return asfloat(i);
}

void comp_main()
{
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= pc_ray_count)
    {
        return;
    }
    uint rb = gid * 8u;
    uint param = _49.Load((rb + 0u) * 4 + 0);
    uint param_1 = _49.Load((rb + 1u) * 4 + 0);
    uint param_2 = _49.Load((rb + 2u) * 4 + 0);
    float3 org = float3(fu(param), fu(param_1), fu(param_2));
    uint param_3 = _49.Load((rb + 3u) * 4 + 0);
    float tmin = fu(param_3);
    uint param_4 = _49.Load((rb + 4u) * 4 + 0);
    uint param_5 = _49.Load((rb + 5u) * 4 + 0);
    uint param_6 = _49.Load((rb + 6u) * 4 + 0);
    float3 dir = float3(fu(param_4), fu(param_5), fu(param_6));
    uint param_7 = _49.Load((rb + 7u) * 4 + 0);
    float tmax = fu(param_7);
    float3 invd;
    for (int k = 0; k < 3; k++)
    {
        float d = dir[k];
        float inv = 1.0f / d;
        if (!((inv >= (-999999984306749440.0f)) && (inv <= 999999984306749440.0f)))
        {
            float s = (d == 0.0f) ? 1.0f : d;
            inv = (s < 0.0f) ? (-999999984306749440.0f) : 999999984306749440.0f;
        }
        invd[k] = inv;
    }
    float best_t = tmax;
    float best_u = 0.0f;
    float best_v = 0.0f;
    uint best_prim = 4294967295u;
    uint node_stride = _164;
    uint block_stride = _167;
    uint prim_off = _170;
    int sp = 0;
    uint stk_ref[STACK];
    stk_ref[0] = pc_root;
    float stk_tn[STACK];
    stk_tn[0] = tmin;
    sp = 1;
    float hit_tn[8];
    uint hit_ref[8];
    while (sp > 0)
    {
        sp--;
        uint ref = stk_ref[sp];
        float tnear_e = stk_tn[sp];
        if (tnear_e >= best_t)
        {
            continue;
        }
        if ((ref & 2147483648u) != 0u)
        {
            uint blk0 = (ref & 2147483647u) >> 4u;
            uint nblk = ref & 15u;
            for (uint b = 0u; b < nblk; b++)
            {
                uint bb = (blk0 + b) * block_stride;
                for (uint lane = 0u; lane < W; lane++)
                {
                    uint prim = _249.Load(((bb + prim_off) + lane) * 4 + 0);
                    if (prim == 4294967295u)
                    {
                        continue;
                    }
                    uint param_8 = _249.Load(((bb + _264) + lane) * 4 + 0);
                    float v0x = fu(param_8);
                    uint param_9 = _249.Load(((bb + _274) + lane) * 4 + 0);
                    float v0y = fu(param_9);
                    uint param_10 = _249.Load(((bb + _284) + lane) * 4 + 0);
                    float v0z = fu(param_10);
                    uint param_11 = _249.Load(((bb + _294) + lane) * 4 + 0);
                    float e1x = fu(param_11);
                    uint param_12 = _249.Load(((bb + _304) + lane) * 4 + 0);
                    float e1y = fu(param_12);
                    uint param_13 = _249.Load(((bb + _314) + lane) * 4 + 0);
                    float e1z = fu(param_13);
                    uint param_14 = _249.Load(((bb + _324) + lane) * 4 + 0);
                    float e2x = fu(param_14);
                    uint param_15 = _249.Load(((bb + _334) + lane) * 4 + 0);
                    float e2y = fu(param_15);
                    uint param_16 = _249.Load(((bb + _344) + lane) * 4 + 0);
                    float e2z = fu(param_16);
                    float px = (dir.y * e2z) - (dir.z * e2y);
                    float py = (dir.z * e2x) - (dir.x * e2z);
                    float pz = (dir.x * e2y) - (dir.y * e2x);
                    float det = ((e1x * px) + (e1y * py)) + (e1z * pz);
                    if ((det > (-9.9999999600419720025001879548654e-13f)) && (det < 9.9999999600419720025001879548654e-13f))
                    {
                        continue;
                    }
                    float inv_det = 1.0f / det;
                    float tvx = org.x - v0x;
                    float tvy = org.y - v0y;
                    float tvz = org.z - v0z;
                    float uu = (((tvx * px) + (tvy * py)) + (tvz * pz)) * inv_det;
                    if ((uu < 0.0f) || (uu > 1.0f))
                    {
                        continue;
                    }
                    float qx = (tvy * e1z) - (tvz * e1y);
                    float qy = (tvz * e1x) - (tvx * e1z);
                    float qz = (tvx * e1y) - (tvy * e1x);
                    float vv = (((dir.x * qx) + (dir.y * qy)) + (dir.z * qz)) * inv_det;
                    bool _486 = vv < 0.0f;
                    bool _494;
                    if (!_486)
                    {
                        _494 = (uu + vv) > 1.0f;
                    }
                    else
                    {
                        _494 = _486;
                    }
                    if (_494)
                    {
                        continue;
                    }
                    float tt = (((e2x * qx) + (e2y * qy)) + (e2z * qz)) * inv_det;
                    if ((tt < tmin) || (tt >= best_t))
                    {
                        continue;
                    }
                    best_t = tt;
                    best_u = uu;
                    best_v = vv;
                    best_prim = prim;
                }
            }
            continue;
        }
        uint nb = (ref & 2147483647u) * node_stride;
        uint nchildren = _540.Load((nb + _542) * 4 + 0);
        int nhit = 0;
        for (uint i = 0u; i < nchildren; i++)
        {
            uint param_17 = _540.Load(((nb + _558) + i) * 4 + 0);
            float lo_x = fu(param_17);
            uint param_18 = _540.Load(((nb + _568) + i) * 4 + 0);
            float lo_y = fu(param_18);
            uint param_19 = _540.Load(((nb + _578) + i) * 4 + 0);
            float lo_z = fu(param_19);
            uint param_20 = _540.Load(((nb + _588) + i) * 4 + 0);
            float hi_x = fu(param_20);
            uint param_21 = _540.Load(((nb + _598) + i) * 4 + 0);
            float hi_y = fu(param_21);
            uint param_22 = _540.Load(((nb + _608) + i) * 4 + 0);
            float hi_z = fu(param_22);
            float tlx = (lo_x - org.x) * invd.x;
            float thx = (hi_x - org.x) * invd.x;
            float tly = (lo_y - org.y) * invd.y;
            float thy = (hi_y - org.y) * invd.y;
            float tlz = (lo_z - org.z) * invd.z;
            float thz = (hi_z - org.z) * invd.z;
            float tnx = min(tlx, thx);
            float tfx = max(tlx, thx);
            float tny = min(tly, thy);
            float tfy = max(tly, thy);
            float tnz = min(tlz, thz);
            float tfz = max(tlz, thz);
            float tnear = max(max(tnx, tny), max(tnz, tmin));
            float tfar = min(min(tfx, tfy), min(tfz, best_t));
            if (tnear <= tfar)
            {
                int _710 = nhit;
                nhit = _710 + 1;
                int j = _710;
                for (;;)
                {
                    bool _718 = j > 0;
                    bool _730;
                    if (_718)
                    {
                        _730 = hit_tn[j - 1] > tnear;
                    }
                    else
                    {
                        _730 = _718;
                    }
                    if (_730)
                    {
                        hit_tn[j] = hit_tn[j - 1];
                        hit_ref[j] = hit_ref[j - 1];
                        j--;
                        continue;
                    }
                    else
                    {
                        break;
                    }
                }
                hit_tn[j] = tnear;
                hit_ref[j] = _540.Load(((nb + _753) + i) * 4 + 0);
            }
        }
        if ((uint(sp) + uint(nhit)) > STACK)
        {
            best_prim = 4294967295u;
            break;
        }
        int _773 = nhit - 1;
        for (int i_1 = _773; i_1 >= 0; i_1--)
        {
            stk_ref[sp] = hit_ref[i_1];
            stk_tn[sp] = hit_tn[i_1];
            sp++;
        }
    }
    uint hb = gid * 4u;
    _801.Store((hb + 0u) * 4 + 0, asuint((best_prim != 4294967295u) ? best_t : 0.0f));
    _801.Store((hb + 1u) * 4 + 0, asuint(best_u));
    _801.Store((hb + 2u) * 4 + 0, asuint(best_v));
    _801.Store((hb + 3u) * 4 + 0, best_prim);
}

[numthreads(64, 1, 1)]
void main(SPIRV_Cross_Input stage_input)
{
    gl_GlobalInvocationID = stage_input.gl_GlobalInvocationID;
    comp_main();
}
)HLSLSRC";
