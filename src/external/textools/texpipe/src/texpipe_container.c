/*
 * TinyEXR texpipe - multi-mip (and, in Phase 3, multi-face) container writers.
 *
 * DDS (DX10 header) for the BC family, KTX2 for everything (BC / ETC2 / EAC /
 * ASTC). Kept separate from texcomp's single-surface writers so texcomp's
 * shipped API is untouched.
 *
 * Copyright (c) 2014-2026 Syoyo Fujita and TinyEXR authors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "texpipe_internal.h"

#include <string.h>

/* ================================================================== DDS */

/* DDS flag / caps constants. */
#define TP_DDSD_CAPS 0x1u
#define TP_DDSD_HEIGHT 0x2u
#define TP_DDSD_WIDTH 0x4u
#define TP_DDSD_PIXELFORMAT 0x1000u
#define TP_DDSD_MIPMAPCOUNT 0x20000u
#define TP_DDSD_LINEARSIZE 0x80000u
#define TP_DDSCAPS_COMPLEX 0x8u
#define TP_DDSCAPS_TEXTURE 0x1000u
#define TP_DDSCAPS_MIPMAP 0x400000u
#define TP_DDSCAPS2_CUBEMAP 0x200u
#define TP_DDSCAPS2_CUBEMAP_ALLFACES 0xFC00u /* +X..-Z */
#define TP_DDPF_FOURCC 0x4u
#define TP_DDS_MISC_TEXTURECUBE 0x4u

static size_t tp_dds_payload_size(const tp_blocks *b) {
    size_t total = 0;
    int i, n = b->num_faces * b->num_levels;
    for (i = 0; i < n; ++i) total += b->blk[i].size;
    return total;
}

size_t tp_dds_size(const tp_blocks *b, const tp_options *opt) {
    tp_codec_desc d;
    if (!b || !opt) return 0;
    if (!TP_OK(tp_codec_describe(opt->codec, opt, &d)) || d.dxgi_format == 0u)
        return 0; /* not a DDS/BC-representable codec */
    return 148u + tp_dds_payload_size(b);
}

tp_result tp_dds_write(const tp_blocks *b, const tp_options *opt, uint8_t *out,
                       size_t out_size, size_t *written) {
    tp_codec_desc d;
    size_t need, off;
    int face, level;
    uint32_t caps, caps2 = 0u, flags, misc = 0u;
    if (!b || !opt || !out) return TP_ERROR_INVALID_ARGUMENT;
    if (!TP_OK(tp_codec_describe(opt->codec, opt, &d)) || d.dxgi_format == 0u)
        return TP_ERROR_UNSUPPORTED;
    need = 148u + tp_dds_payload_size(b);
    if (out_size < need) return TP_ERROR_INVALID_ARGUMENT;

    memset(out, 0, 148);
    memcpy(out, "DDS ", 4);
    tp_wr_u32(out + 4, 124u);
    flags = TP_DDSD_CAPS | TP_DDSD_HEIGHT | TP_DDSD_WIDTH | TP_DDSD_PIXELFORMAT |
            TP_DDSD_LINEARSIZE;
    if (b->num_levels > 1) flags |= TP_DDSD_MIPMAPCOUNT;
    tp_wr_u32(out + 8, flags);
    tp_wr_u32(out + 12, b->blk[0].height);
    tp_wr_u32(out + 16, b->blk[0].width);
    tp_wr_u32(out + 20, (uint32_t)b->blk[0].size); /* level 0 linear size */
    tp_wr_u32(out + 28, (uint32_t)b->num_levels);
    tp_wr_u32(out + 76, 32u);              /* ddspf.dwSize */
    tp_wr_u32(out + 80, TP_DDPF_FOURCC);   /* ddspf.dwFlags */
    memcpy(out + 84, "DX10", 4);           /* ddspf.dwFourCC */
    caps = TP_DDSCAPS_TEXTURE;
    if (b->num_levels > 1) caps |= TP_DDSCAPS_COMPLEX | TP_DDSCAPS_MIPMAP;
    if (b->num_faces == 6) {
        caps |= TP_DDSCAPS_COMPLEX;
        caps2 = TP_DDSCAPS2_CUBEMAP | TP_DDSCAPS2_CUBEMAP_ALLFACES;
        misc = TP_DDS_MISC_TEXTURECUBE;
    }
    tp_wr_u32(out + 108, caps);
    tp_wr_u32(out + 112, caps2);
    /* DDS_HEADER_DXT10 begins at offset 128. */
    tp_wr_u32(out + 128, d.dxgi_format);
    tp_wr_u32(out + 132, 3u);                        /* D3D11_RESOURCE_DIMENSION_TEXTURE2D */
    tp_wr_u32(out + 136, misc);
    tp_wr_u32(out + 140, 1u);                        /* arraySize */
    tp_wr_u32(out + 144, 0u);

    /* Payload: face-major, level 0 (largest) first — matches tp_blocks order. */
    off = 148u;
    for (face = 0; face < b->num_faces; ++face) {
        for (level = 0; level < b->num_levels; ++level) {
            const tp_block_level *bl = &b->blk[face * b->num_levels + level];
            memcpy(out + off, bl->data, bl->size);
            off += bl->size;
        }
    }
    if (written) *written = need;
    return TP_SUCCESS;
}

/* ================================================================= KTX2 */

/* KHR Data Format color models. */
#define TP_KDF_MODEL_BC1A 128u
#define TP_KDF_MODEL_BC3 130u
#define TP_KDF_MODEL_BC5 132u
#define TP_KDF_MODEL_BC6H 133u
#define TP_KDF_MODEL_BC7 134u
#define TP_KDF_MODEL_ETC2 161u
#define TP_KDF_MODEL_ASTC 162u
#define TP_KDF_PRIMARIES_BT709 1u
#define TP_KDF_TRANSFER_LINEAR 1u
#define TP_KDF_TRANSFER_SRGB 2u

static uint32_t tp_kdf_model(tp_codec codec) {
    switch (codec) {
    case TP_CODEC_BC1: return TP_KDF_MODEL_BC1A;
    case TP_CODEC_BC3: return TP_KDF_MODEL_BC3;
    case TP_CODEC_BC5: return TP_KDF_MODEL_BC5;
    case TP_CODEC_BC6H: return TP_KDF_MODEL_BC6H;
    case TP_CODEC_BC7: return TP_KDF_MODEL_BC7;
    case TP_CODEC_ETC2_RGB:
    case TP_CODEC_ETC2_RGBA:
    case TP_CODEC_EAC_R11:
    case TP_CODEC_EAC_RG11: return TP_KDF_MODEL_ETC2;
    case TP_CODEC_ASTC:
    case TP_CODEC_ASTC_HDR: return TP_KDF_MODEL_ASTC;
    }
    return 0u;
}

/* One basic descriptor block with a single (block-spanning) sample. */
#define TP_DFD_SAMPLES 1u
#define TP_DFD_BLOCK_SIZE (24u + 16u * TP_DFD_SAMPLES)
#define TP_DFD_TOTAL (4u + TP_DFD_BLOCK_SIZE)

static void tp_write_dfd(uint8_t *p, const tp_codec_desc *d, tp_codec codec,
                         int srgb) {
    uint32_t model = tp_kdf_model(codec);
    uint32_t transfer = srgb ? TP_KDF_TRANSFER_SRGB : TP_KDF_TRANSFER_LINEAR;
    uint8_t bitlen = (uint8_t)(d->block_bytes * 8 - 1);
    if (d->is_hdr) transfer = TP_KDF_TRANSFER_LINEAR;
    memset(p, 0, TP_DFD_TOTAL);
    tp_wr_u32(p + 0, TP_DFD_TOTAL);
    /* descriptor block */
    tp_wr_u32(p + 4, 0u);                             /* vendorId|descriptorType */
    tp_wr_u32(p + 8, 2u | (TP_DFD_BLOCK_SIZE << 16)); /* version | blockSize */
    p[12] = (uint8_t)model;
    p[13] = (uint8_t)TP_KDF_PRIMARIES_BT709;
    p[14] = (uint8_t)transfer;
    p[15] = 0u;                                       /* flags */
    p[16] = (uint8_t)(d->block_w - 1);                /* texelBlockDimension0 */
    p[17] = (uint8_t)(d->block_h - 1);
    p[18] = 0u;
    p[19] = 0u;
    p[20] = (uint8_t)d->block_bytes;                  /* bytesPlane0 */
    /* bytesPlane1..7 = 0, texelBlockDimension2..3 = 0 (already memset) */
    /* sample 0 at offset 28 */
    p[28] = 0u; p[29] = 0u;                           /* bitOffset = 0 */
    p[30] = bitlen;                                   /* bitLength */
    p[31] = 0u;                                       /* channelType */
    /* samplePosition0..3 = 0 (p[32..35]) */
    tp_wr_u32(p + 36, 0u);                            /* sampleLower */
    tp_wr_u32(p + 40, 0xffffffffu);                   /* sampleUpper */
}

static size_t tp_align_up(size_t v, size_t a) {
    if (a <= 1) return v;
    return (v + (a - 1)) / a * a;
}

/* Byte length of one mip level = sum of all faces at that level. */
static size_t tp_ktx2_level_len(const tp_blocks *b, int level) {
    size_t total = 0;
    int face;
    for (face = 0; face < b->num_faces; ++face)
        total += b->blk[face * b->num_levels + level].size;
    return total;
}

/* Compute the file layout: data region base + per-level absolute offsets
 * (levels stored smallest-first, aligned to the texel block size). Returns the
 * total file size; fills level_off/level_len if non-NULL. */
static size_t tp_ktx2_layout(const tp_blocks *b, const tp_codec_desc *d,
                             uint64_t *level_off, uint64_t *level_len) {
    size_t idx_bytes = (size_t)b->num_levels * 24u;
    size_t align = (size_t)d->block_bytes;
    size_t cursor = 80u + idx_bytes + TP_DFD_TOTAL; /* + kvd(0) + sgd(0) */
    int ll;
    if (align < 4u) align = 4u;
    for (ll = b->num_levels - 1; ll >= 0; --ll) {
        size_t len = tp_ktx2_level_len(b, ll);
        cursor = tp_align_up(cursor, align);
        if (level_off) level_off[ll] = cursor;
        if (level_len) level_len[ll] = len;
        cursor += len;
    }
    return cursor;
}

size_t tp_ktx2_size(const tp_blocks *b, const tp_options *opt) {
    tp_codec_desc d;
    if (!b || !opt) return 0;
    if (!TP_OK(tp_codec_describe(opt->codec, opt, &d)) || d.vk_format == 0u) return 0;
    return tp_ktx2_layout(b, &d, NULL, NULL);
}

tp_result tp_ktx2_write(const tp_blocks *b, const tp_options *opt, uint8_t *out,
                        size_t out_size, size_t *written) {
    static const uint8_t id[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32,
                                   0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    tp_codec_desc d;
    uint64_t level_off[32], level_len[32];
    size_t need, idx_base, dfd_off;
    int ll, face;
    if (!b || !opt || !out) return TP_ERROR_INVALID_ARGUMENT;
    if (b->num_levels > 32) return TP_ERROR_UNSUPPORTED;
    if (!TP_OK(tp_codec_describe(opt->codec, opt, &d)) || d.vk_format == 0u)
        return TP_ERROR_UNSUPPORTED;
    need = tp_ktx2_layout(b, &d, level_off, level_len);
    if (out_size < need) return TP_ERROR_INVALID_ARGUMENT;
    memset(out, 0, need);

    memcpy(out, id, 12);
    tp_wr_u32(out + 12, d.vk_format);
    tp_wr_u32(out + 16, 1u);                      /* typeSize */
    tp_wr_u32(out + 20, b->blk[0].width);
    tp_wr_u32(out + 24, b->blk[0].height);
    tp_wr_u32(out + 28, 0u);                      /* pixelDepth */
    tp_wr_u32(out + 32, 0u);                      /* layerCount (0 = non-array) */
    tp_wr_u32(out + 36, (uint32_t)b->num_faces);  /* faceCount */
    tp_wr_u32(out + 40, (uint32_t)b->num_levels); /* levelCount */
    tp_wr_u32(out + 44, 0u);                      /* supercompressionScheme */

    idx_base = 80u;
    dfd_off = idx_base + (size_t)b->num_levels * 24u;
    tp_wr_u32(out + 48, (uint32_t)dfd_off);       /* dfdByteOffset */
    tp_wr_u32(out + 52, TP_DFD_TOTAL);            /* dfdByteLength */
    tp_wr_u32(out + 56, 0u);                      /* kvdByteOffset */
    tp_wr_u32(out + 60, 0u);                      /* kvdByteLength */
    tp_wr_u64(out + 64, 0u);                      /* sgdByteOffset */
    tp_wr_u64(out + 72, 0u);                      /* sgdByteLength */

    /* Level index: one entry per level, index 0 = base (largest). */
    for (ll = 0; ll < b->num_levels; ++ll) {
        uint8_t *e = out + idx_base + (size_t)ll * 24u;
        tp_wr_u64(e + 0, level_off[ll]);
        tp_wr_u64(e + 8, level_len[ll]);
        tp_wr_u64(e + 16, level_len[ll]); /* uncompressedByteLength */
    }

    tp_write_dfd(out + dfd_off, &d, opt->codec, opt->srgb);

    /* Level data: for each level, faces contiguous (KTX2 order layer,face,z). */
    for (ll = 0; ll < b->num_levels; ++ll) {
        size_t off = (size_t)level_off[ll];
        for (face = 0; face < b->num_faces; ++face) {
            const tp_block_level *bl = &b->blk[face * b->num_levels + ll];
            memcpy(out + off, bl->data, bl->size);
            off += bl->size;
        }
    }
    if (written) *written = need;
    return TP_SUCCESS;
}

/* ===================================================== KTX2 reading / decode */

static uint32_t tp_rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t tp_rd_u64(const uint8_t *p) {
    return (uint64_t)tp_rd_u32(p) | ((uint64_t)tp_rd_u32(p + 4) << 32);
}

/* Map a texcomp tc_result onto the texpipe tp_result enum (their negative codes
 * are ordered differently). */
static tp_result tp_from_tc(tc_result r) {
    switch (r) {
    case TC_SUCCESS: return TP_SUCCESS;
    case TC_ERROR_INVALID_ARGUMENT: return TP_ERROR_INVALID_ARGUMENT;
    case TC_ERROR_OUT_OF_MEMORY: return TP_ERROR_OUT_OF_MEMORY;
    case TC_ERROR_IO: return TP_ERROR_IO;
    case TC_ERROR_UNSUPPORTED: return TP_ERROR_UNSUPPORTED;
    case TC_ERROR_CORRUPT: return TP_ERROR_INVALID_ARGUMENT;
    }
    return TP_ERROR_UNSUPPORTED;
}

static const uint8_t tp_ktx2_id[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32,
                                       0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};

/* Tightly-packed byte size of one mip level for the parsed image's format;
 * used to reject crafted files whose declared dimensions exceed the payload.
 * The caller has already clamped width/height to TP_KTX2_MAX_DIM, faces to 6 and
 * layers to TP_KTX2_MAX_LAYERS, so the product stays far below 2^64 (worst case
 * 2^14 * 2^14 blocks * 16 B * 6 * 2^11 = 2^46) and cannot wrap. */
static uint64_t tp_ktx2_level_expected(const tp_ktx2_image *img, uint32_t w,
                                       uint32_t h) {
    uint64_t bw = ((uint64_t)w + (uint64_t)img->block_w - 1u) / (uint64_t)img->block_w;
    uint64_t bh = ((uint64_t)h + (uint64_t)img->block_h - 1u) / (uint64_t)img->block_h;
    uint64_t e = bw * bh * (uint64_t)img->block_bytes;
    if ((uint64_t)img->num_faces > 1u) e *= (uint64_t)img->num_faces;
    if (img->num_layers > 1) e *= (uint64_t)img->num_layers;
    return e;
}

tp_result tp_ktx2_read(const uint8_t *data, size_t size, tp_ktx2_image *out) {
    return tp_ktx2_read_zstd(data, size, NULL, NULL, NULL, out);
}

void tp_ktx2_image_free(const tir_allocator *a, tp_ktx2_image *img) {
    if (img && img->_owned) {
        tp_dealloc(a, img->_owned);
        img->_owned = NULL;
    }
}

tp_result tp_ktx2_read_zstd(const uint8_t *data, size_t size,
                            const tir_allocator *a, tp_zstd_decompress_fn zdec,
                            void *user, tp_ktx2_image *out) {
    uint32_t vk, layer_count, face_count, level_count, scheme;
    uint32_t dfd_off, dfd_len;
    int nlev, l;
    if (!data || !out) return TP_ERROR_INVALID_ARGUMENT;
    if (size < 80u || memcmp(data, tp_ktx2_id, 12) != 0)
        return TP_ERROR_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));

    vk = tp_rd_u32(data + 12);
    out->width = tp_rd_u32(data + 20);
    out->height = tp_rd_u32(data + 24);
    layer_count = tp_rd_u32(data + 32);
    face_count = tp_rd_u32(data + 36);
    level_count = tp_rd_u32(data + 40);
    scheme = tp_rd_u32(data + 44);
    dfd_off = tp_rd_u32(data + 48);
    dfd_len = tp_rd_u32(data + 52);
    out->supercompression = scheme;
    if (scheme == 1u) return TP_ERROR_UNSUPPORTED;          /* BasisLZ */
    if (scheme == 2u && !zdec) return TP_ERROR_UNSUPPORTED; /* need a decompressor */
    if (scheme != 0u && scheme != 2u) return TP_ERROR_UNSUPPORTED;

    /* Range-check the header counts while still unsigned: narrowing a hostile
     * 32-bit field to int first would turn it negative and slip past the limits
     * (and past the num_layers/num_faces guards downstream). */
    if (out->width == 0u || out->height == 0u ||
        out->width > (uint32_t)TP_KTX2_MAX_DIM ||
        out->height > (uint32_t)TP_KTX2_MAX_DIM)
        return TP_ERROR_UNSUPPORTED;
    if (level_count > (uint32_t)TP_KTX2_MAX_LEVELS) return TP_ERROR_UNSUPPORTED;
    if (face_count > 6u) return TP_ERROR_UNSUPPORTED;
    if (layer_count > (uint32_t)TP_KTX2_MAX_LAYERS) return TP_ERROR_UNSUPPORTED;

    nlev = level_count ? (int)level_count : 1; /* 0 = "generate", treat as 1 */
    out->num_levels = nlev;
    out->num_faces = face_count ? (int)face_count : 1;
    out->num_layers = (int)layer_count;
    out->vk_format = vk;

    if (size < 80u + (size_t)nlev * 24u) return TP_ERROR_INVALID_ARGUMENT;

    /* The DFD is not parsed (vkFormat carries everything we need), but a file
     * claiming one outside the blob is malformed. */
    if (dfd_len != 0u &&
        ((uint64_t)dfd_off + (uint64_t)dfd_len > (uint64_t)size ||
         dfd_off < 80u + (uint64_t)nlev * 24u))
        return TP_ERROR_INVALID_ARGUMENT;

    if (vk == 0u) {
        out->is_uni = 1;              /* UASTC transcodable intermediate */
        out->block_w = 4;
        out->block_h = 4;
        out->block_bytes = 16;
    } else {
        tp_codec_desc d;
        tp_codec c;
        int srgb;
        if (!TP_OK(tp_vk_format_describe(vk, &d, &c, &srgb)))
            return TP_ERROR_UNSUPPORTED;
        out->codec = c;
        out->srgb = srgb;
        out->is_hdr = d.is_hdr;
        out->block_w = d.block_w;
        out->block_h = d.block_h;
        out->block_bytes = d.block_bytes;
    }

    if (scheme == 0u) {
        /* Zero-copy: level `data` pointers alias the input. */
        for (l = 0; l < nlev; ++l) {
            const uint8_t *e = data + 80u + (size_t)l * 24u;
            uint64_t off = tp_rd_u64(e + 0);
            uint64_t len = tp_rd_u64(e + 8);
            uint32_t w = out->width >> l, h = out->height >> l;
            if (!w) w = 1u;
            if (!h) h = 1u;
            if (off > size || len > (uint64_t)size - off)
                return TP_ERROR_INVALID_ARGUMENT; /* out-of-bounds level */
            if (len < tp_ktx2_level_expected(out, w, h))
                return TP_ERROR_INVALID_ARGUMENT; /* truncated level */
            out->levels[l].data = data + (size_t)off;
            out->levels[l].size = (size_t)len;
            out->levels[l].width = w;
            out->levels[l].height = h;
        }
        return TP_SUCCESS;
    }

    /* scheme == 2 (Zstd): decompress every level into one owned buffer. */
    {
        uint64_t ulen[TP_KTX2_MAX_LEVELS];
        uint64_t total = 0;
        uint8_t *owned;
        size_t cursor = 0;
        /* uncompressedByteLength must be *exactly* the inflated level size (per
         * KTX2: the levels are tightly packed block data). Pinning it rather
         * than merely lower-bounding it is what keeps `total` bounded by the
         * dimension caps: a hostile pair of huge lengths would otherwise wrap
         * the sum, yielding a small `owned` that zdec then writes far past --
         * it is handed `ulen` as its destination capacity. */
        for (l = 0; l < nlev; ++l) {
            uint32_t w = out->width >> l, h = out->height >> l;
            if (!w) w = 1u;
            if (!h) h = 1u;
            ulen[l] = tp_rd_u64(data + 80u + (size_t)l * 24u + 16u);
            if (ulen[l] != tp_ktx2_level_expected(out, w, h))
                return TP_ERROR_INVALID_ARGUMENT;
            total += ulen[l]; /* <= TP_KTX2_MAX_LEVELS * 2^46: cannot wrap */
        }
        if (total == 0u || total > (uint64_t)(size_t)-1)
            return TP_ERROR_INVALID_ARGUMENT;
        owned = (uint8_t *)tp_alloc(a, (size_t)total);
        if (!owned) return TP_ERROR_OUT_OF_MEMORY;
        for (l = 0; l < nlev; ++l) {
            const uint8_t *e = data + 80u + (size_t)l * 24u;
            uint64_t off = tp_rd_u64(e + 0);
            uint64_t clen = tp_rd_u64(e + 8);
            uint32_t w = out->width >> l, h = out->height >> l;
            size_t got;
            if (!w) w = 1u;
            if (!h) h = 1u;
            if (off > size || clen > (uint64_t)size - off) {
                tp_dealloc(a, owned);
                return TP_ERROR_INVALID_ARGUMENT;
            }
            got = zdec(user, owned + cursor, (size_t)ulen[l], data + (size_t)off,
                       (size_t)clen);
            if (got != (size_t)ulen[l]) {
                tp_dealloc(a, owned);
                return TP_ERROR_INVALID_ARGUMENT;
            }
            out->levels[l].data = owned + cursor;
            out->levels[l].size = (size_t)ulen[l];
            out->levels[l].width = w;
            out->levels[l].height = h;
            cursor += (size_t)ulen[l];
        }
        out->_owned = owned;
        return TP_SUCCESS;
    }
}

tp_result tp_ktx2_decode_level_rgba8(const tp_ktx2_image *img, int level,
                                     uint8_t *out_rgba, size_t out_size) {
    uint32_t w, h;
    size_t need;
    const uint8_t *blocks;
    if (!img || !out_rgba) return TP_ERROR_INVALID_ARGUMENT;
    if (level < 0 || level >= img->num_levels) return TP_ERROR_INVALID_ARGUMENT;
    if (img->num_faces != 1 || img->num_layers > 1)
        return TP_ERROR_UNSUPPORTED; /* single-face / non-array only for now */
    w = img->levels[level].width;
    h = img->levels[level].height;
    if (!w || !h) return TP_ERROR_INVALID_ARGUMENT;
    /* w*h*4 is computed in 64 bits first: on a 32-bit size_t the RGBA8 surface
     * for a large level wraps, which would let the out_size check pass. */
    if ((uint64_t)w * (uint64_t)h * 4u > (uint64_t)(size_t)-1)
        return TP_ERROR_UNSUPPORTED;
    need = (size_t)w * (size_t)h * 4u;
    if (out_size < need) return TP_ERROR_INVALID_ARGUMENT;
    blocks = img->levels[level].data;

    if (img->is_uni)
        return tp_from_tc(tc_uni_decompress_rgba8(blocks, w, h, (size_t)w * 4u,
                                                  out_rgba, out_size));

    switch (img->codec) {
    case TP_CODEC_BC7:
        return tp_from_tc(tc_bc7_decompress_rgba8(blocks, w, h, (size_t)w * 4u,
                                                  out_rgba, out_size));
    case TP_CODEC_ASTC:
        return tp_from_tc(tc_astc_decompress_rgba8(
            blocks, w, h, (uint32_t)img->block_w, (uint32_t)img->block_h,
            out_rgba, out_size));
    default:
        /* BC1/3/5/6H, ETC2/EAC: no library decoder — upload blocks directly or
         * transcode a uni source instead. */
        return TP_ERROR_UNSUPPORTED;
    }
}

/* ---- uni (UASTC) KTX2 writer: vkFormat=UNDEFINED, supercompression=0 ---- */

/* KHR_DF UASTC descriptor (44 bytes), matching the uni-in-KTX2 convention. */
static void tp_write_uni_dfd(uint8_t *p) {
    memset(p, 0, 44);
    tp_wr_u32(p + 0, 44u);                 /* dfdTotalSize */
    tp_wr_u32(p + 8, 2u | (40u << 16));    /* version | descriptorBlockSize */
    p[12] = 166u;                          /* KHR_DF_MODEL_UASTC */
    p[13] = 1u;                            /* primaries BT709 */
    p[14] = 1u;                            /* transfer LINEAR */
    p[16] = 3u; p[17] = 3u;                /* texelBlockDimension = 4x4 */
    p[20] = 16u;                           /* bytesPlane0 */
    p[30] = 127u;                          /* sample bitLength (128 bits) */
    tp_wr_u32(p + 40, 0xffffffffu);        /* sampleUpper */
}

/* Total KTX2 byte size for `n` uni levels, filling loff[] with each level's
 * offset. Returns 0 if the layout overflows size_t (uni_sizes[] is caller data
 * on the public tp_ktx2_write_uni path, so it is not trusted to be sane). */
static size_t tp_ktx2_uni_layout(const size_t *sizes, int n, uint64_t *loff) {
    size_t cursor = 80u + (size_t)n * 24u + 44u; /* + kvd(0) + sgd(0) */
    const size_t smax = (size_t)-1;
    int l;
    for (l = n - 1; l >= 0; --l) {          /* smallest-first, aligned to 16 */
        if (cursor > smax - 15u) return 0;
        cursor = tp_align_up(cursor, 16u);
        if (sizes[l] > smax - cursor) return 0;
        if (loff) loff[l] = cursor;
        cursor += sizes[l];
    }
    return cursor;
}

size_t tp_ktx2_uni_size(const size_t *sizes, int num_levels) {
    if (!sizes || num_levels < 1 || num_levels > TP_KTX2_MAX_LEVELS) return 0;
    return tp_ktx2_uni_layout(sizes, num_levels, NULL);
}

tp_result tp_ktx2_write_uni(const uint8_t *const *uni_levels,
                            const size_t *uni_sizes, const uint32_t *level_w,
                            const uint32_t *level_h, int num_levels,
                            uint8_t *out, size_t out_size, size_t *written) {
    uint64_t loff[TP_KTX2_MAX_LEVELS];
    size_t need, dfd_off;
    int l;
    if (!uni_levels || !uni_sizes || !level_w || !level_h || !out)
        return TP_ERROR_INVALID_ARGUMENT;
    if (num_levels < 1 || num_levels > TP_KTX2_MAX_LEVELS)
        return TP_ERROR_INVALID_ARGUMENT;
    need = tp_ktx2_uni_layout(uni_sizes, num_levels, loff);
    if (need == 0u) return TP_ERROR_INVALID_ARGUMENT; /* layout overflowed */
    if (out_size < need) return TP_ERROR_INVALID_ARGUMENT;
    memset(out, 0, need);

    memcpy(out, tp_ktx2_id, 12);
    tp_wr_u32(out + 12, 0u);                      /* vkFormat = UNDEFINED */
    tp_wr_u32(out + 16, 1u);                      /* typeSize */
    tp_wr_u32(out + 20, level_w[0]);
    tp_wr_u32(out + 24, level_h[0]);
    tp_wr_u32(out + 28, 0u);                      /* pixelDepth */
    tp_wr_u32(out + 32, 0u);                      /* layerCount */
    tp_wr_u32(out + 36, 1u);                      /* faceCount */
    tp_wr_u32(out + 40, (uint32_t)num_levels);
    tp_wr_u32(out + 44, 0u);                      /* supercompressionScheme */
    dfd_off = 80u + (size_t)num_levels * 24u;
    tp_wr_u32(out + 48, (uint32_t)dfd_off);       /* dfdByteOffset */
    tp_wr_u32(out + 52, 44u);                     /* dfdByteLength */
    tp_wr_u32(out + 56, 0u);                      /* kvdByteOffset */
    tp_wr_u32(out + 60, 0u);                      /* kvdByteLength */
    tp_wr_u64(out + 64, 0u);                      /* sgdByteOffset */
    tp_wr_u64(out + 72, 0u);                      /* sgdByteLength */
    for (l = 0; l < num_levels; ++l) {
        uint8_t *e = out + 80u + (size_t)l * 24u;
        tp_wr_u64(e + 0, loff[l]);
        tp_wr_u64(e + 8, (uint64_t)uni_sizes[l]);
        tp_wr_u64(e + 16, (uint64_t)uni_sizes[l]); /* uncompressedByteLength */
    }
    tp_write_uni_dfd(out + dfd_off);
    for (l = 0; l < num_levels; ++l)
        memcpy(out + (size_t)loff[l], uni_levels[l], uni_sizes[l]);
    if (written) *written = need;
    return TP_SUCCESS;
}

/* ---- KTX2 texture arrays (layerCount) ---- */

static size_t tp_ktx2_array_layout(const tp_blocks *layers, int num_layers,
                                   const tp_codec_desc *d, uint64_t *level_off,
                                   uint64_t *level_len) {
    size_t idx_bytes = (size_t)layers[0].num_levels * 24u;
    size_t align = (size_t)d->block_bytes;
    size_t cursor = 80u + idx_bytes + TP_DFD_TOTAL;
    int ll;
    if (align < 4u) align = 4u;
    for (ll = layers[0].num_levels - 1; ll >= 0; --ll) {
        size_t len = (size_t)num_layers * tp_ktx2_level_len(&layers[0], ll);
        cursor = tp_align_up(cursor, align);
        if (level_off) level_off[ll] = cursor;
        if (level_len) level_len[ll] = len;
        cursor += len;
    }
    return cursor;
}

size_t tp_ktx2_array_size(const tp_blocks *layers, int num_layers,
                          const tp_options *opt) {
    tp_codec_desc d;
    if (!layers || num_layers < 1 || !opt) return 0;
    if (!TP_OK(tp_codec_describe(opt->codec, opt, &d)) || d.vk_format == 0u) return 0;
    return tp_ktx2_array_layout(layers, num_layers, &d, NULL, NULL);
}

tp_result tp_write_ktx2_array(const tp_blocks *layers, int num_layers,
                              const tp_options *opt, uint8_t *out,
                              size_t out_size, size_t *written) {
    static const uint8_t id[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32,
                                   0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    tp_codec_desc d;
    uint64_t level_off[32], level_len[32];
    size_t need, idx_base, dfd_off;
    int ll, layer, face, nlev, nface, li;
    if (!layers || num_layers < 1 || !opt || !out) return TP_ERROR_INVALID_ARGUMENT;
    nlev = layers[0].num_levels;
    nface = layers[0].num_faces;
    if (nlev > 32) return TP_ERROR_UNSUPPORTED;
    for (li = 1; li < num_layers; ++li)
        if (layers[li].num_levels != nlev || layers[li].num_faces != nface ||
            layers[li].codec != layers[0].codec)
            return TP_ERROR_INVALID_ARGUMENT;
    if (!TP_OK(tp_codec_describe(opt->codec, opt, &d)) || d.vk_format == 0u)
        return TP_ERROR_UNSUPPORTED;
    need = tp_ktx2_array_layout(layers, num_layers, &d, level_off, level_len);
    if (out_size < need) return TP_ERROR_INVALID_ARGUMENT;
    memset(out, 0, need);

    memcpy(out, id, 12);
    tp_wr_u32(out + 12, d.vk_format);
    tp_wr_u32(out + 16, 1u);
    tp_wr_u32(out + 20, layers[0].blk[0].width);
    tp_wr_u32(out + 24, layers[0].blk[0].height);
    tp_wr_u32(out + 28, 0u);
    tp_wr_u32(out + 32, (uint32_t)num_layers); /* layerCount */
    tp_wr_u32(out + 36, (uint32_t)nface);
    tp_wr_u32(out + 40, (uint32_t)nlev);
    tp_wr_u32(out + 44, 0u);
    idx_base = 80u;
    dfd_off = idx_base + (size_t)nlev * 24u;
    tp_wr_u32(out + 48, (uint32_t)dfd_off);
    tp_wr_u32(out + 52, TP_DFD_TOTAL);
    tp_wr_u32(out + 56, 0u);
    tp_wr_u32(out + 60, 0u);
    tp_wr_u64(out + 64, 0u);
    tp_wr_u64(out + 72, 0u);
    for (ll = 0; ll < nlev; ++ll) {
        uint8_t *e = out + idx_base + (size_t)ll * 24u;
        tp_wr_u64(e + 0, level_off[ll]);
        tp_wr_u64(e + 8, level_len[ll]);
        tp_wr_u64(e + 16, level_len[ll]);
    }
    tp_write_dfd(out + dfd_off, &d, opt->codec, opt->srgb);
    /* Level data: for each level, layer-major then face (KTX2 order). */
    for (ll = 0; ll < nlev; ++ll) {
        size_t off = (size_t)level_off[ll];
        for (layer = 0; layer < num_layers; ++layer)
            for (face = 0; face < nface; ++face) {
                const tp_block_level *bl = &layers[layer].blk[face * nlev + ll];
                memcpy(out + off, bl->data, bl->size);
                off += bl->size;
            }
    }
    if (written) *written = need;
    return TP_SUCCESS;
}
