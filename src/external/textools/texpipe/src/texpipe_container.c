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
#define TP_KDF_MODEL_UNSPECIFIED 0u
#define TP_KDF_PRIMARIES_UNSPECIFIED 0u
#define TP_KDF_PRIMARIES_BT709 1u
#define TP_KDF_TRANSFER_LINEAR 1u
#define TP_KDF_TRANSFER_SRGB 2u
/* Private uni channel ids. The private descriptor deliberately uses the
 * UNSPECIFIED model rather than claiming Basis UASTC compatibility. */
#define TP_KDF_CHANNEL_UNI_RGB 0u
#define TP_KDF_CHANNEL_UNI_RGBA 3u

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

/* The dimension range the KTX2 reader accepts. A writer that goes outside it
 * only produces files the library itself refuses to load. */
static int tp_ktx2_dims_ok(uint32_t w, uint32_t h) {
    return w != 0u && h != 0u && w <= (uint32_t)TP_KTX2_MAX_DIM &&
           h <= (uint32_t)TP_KTX2_MAX_DIM;
}

/* The block shape both KTX2 block writers require of a tp_blocks. Checked before
 * anything reads blk[0]: a zeroed tp_blocks (what a tp_compress_chain that
 * failed leaves behind) has num_levels == 0 and a NULL blk, and must produce an
 * error rather than a NULL dereference. */
static int tp_blocks_shape_ok(const tp_blocks *b, const tp_options *opt) {
    if (!b->blk || b->num_levels < 1 || b->num_levels > 32) return 0;
    if (b->num_faces != 1 && b->num_faces != 6) return 0;
    /* The vkFormat and DFD come from opt->codec but the payload bytes come from
     * b: if they disagree, the file says BC1 over BC7 blocks and every reader
     * decodes garbage from it. */
    if (b->codec != opt->codec) return 0;
    return tp_ktx2_dims_ok(b->blk[0].width, b->blk[0].height);
}

/* Byte length of one mip level = sum of all faces at that level. SIZE_MAX marks
 * an overflow: the sizes are caller data, and on a 32-bit size_t (wasm) six
 * faces of a large level can wrap, which would under-size the buffer the payload
 * loop then memcpys into. */
#define TP_SIZE_OVF ((size_t)-1)
static size_t tp_ktx2_level_len(const tp_blocks *b, int level) {
    size_t total = 0;
    int face;
    for (face = 0; face < b->num_faces; ++face) {
        size_t s = b->blk[face * b->num_levels + level].size;
        if (s > TP_SIZE_OVF - total) return TP_SIZE_OVF;
        total += s;
    }
    return total;
}

/* Compute the file layout: data region base + per-level absolute offsets
 * (levels stored smallest-first, aligned to the texel block size). Returns the
 * total file size, or 0 if the layout overflows size_t; fills
 * level_off/level_len if non-NULL. */
static size_t tp_ktx2_layout(const tp_blocks *b, const tp_codec_desc *d,
                             uint64_t *level_off, uint64_t *level_len) {
    size_t idx_bytes = (size_t)b->num_levels * 24u;
    size_t align = (size_t)d->block_bytes;
    size_t cursor = 80u + idx_bytes + TP_DFD_TOTAL; /* + kvd(0) + sgd(0) */
    int ll;
    if (align < 4u) align = 4u;
    for (ll = b->num_levels - 1; ll >= 0; --ll) {
        size_t len = tp_ktx2_level_len(b, ll);
        if (len == TP_SIZE_OVF) return 0;
        if (cursor > TP_SIZE_OVF - (align - 1u)) return 0;
        cursor = tp_align_up(cursor, align);
        if (len > TP_SIZE_OVF - cursor) return 0;
        if (level_off) level_off[ll] = cursor;
        if (level_len) level_len[ll] = len;
        cursor += len;
    }
    return cursor;
}

size_t tp_ktx2_size(const tp_blocks *b, const tp_options *opt) {
    tp_codec_desc d;
    if (!b || !opt || !tp_blocks_shape_ok(b, opt)) return 0;
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
    if (!tp_blocks_shape_ok(b, opt)) return TP_ERROR_UNSUPPORTED;
    if (!TP_OK(tp_codec_describe(opt->codec, opt, &d)) || d.vk_format == 0u)
        return TP_ERROR_UNSUPPORTED;
    need = tp_ktx2_layout(b, &d, level_off, level_len);
    if (need == 0u) return TP_ERROR_INVALID_ARGUMENT; /* layout overflowed */
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

/* Zeroes the whole image, not just the owned block: the level pointers alias
 * either that block or the caller's source buffer, so after this call none of
 * them are safe to follow and the struct must not look usable. */
void tp_ktx2_image_free(const tir_allocator *a, tp_ktx2_image *img) {
    if (!img) return;
    tp_dealloc(a, img->_owned); /* NULL for a zero-copy scheme-0 read */
    memset(img, 0, sizeof(*img));
}

static tp_result tp_ktx2_parse(const uint8_t *data, size_t size,
                               const tir_allocator *a, tp_zstd_decompress_fn zdec,
                               void *user, tp_ktx2_image *out) {
    uint32_t vk, layer_count, face_count, level_count, scheme;
    uint32_t dfd_off, dfd_len, kvd_off, kvd_len;
    int nlev, l;
    memset(out, 0, sizeof(*out));
    if (size < 80u || memcmp(data, tp_ktx2_id, 12) != 0)
        return TP_ERROR_INVALID_ARGUMENT;

    vk = tp_rd_u32(data + 12);
    out->width = tp_rd_u32(data + 20);
    out->height = tp_rd_u32(data + 24);
    layer_count = tp_rd_u32(data + 32);
    face_count = tp_rd_u32(data + 36);
    level_count = tp_rd_u32(data + 40);
    scheme = tp_rd_u32(data + 44);
    dfd_off = tp_rd_u32(data + 48);
    dfd_len = tp_rd_u32(data + 52);
    kvd_off = tp_rd_u32(data + 56);
    kvd_len = tp_rd_u32(data + 60);
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

    /* For a real vkFormat the DFD is redundant (the format carries everything),
     * but a uni file has no format, so the uni branch below reads its transfer
     * and channelType. This bound is what makes those reads safe: with dfd_len
     * >= 44 the whole descriptor is inside the blob. A file claiming a DFD
     * outside it is malformed either way. */
    if (dfd_len != 0u &&
        ((uint64_t)dfd_off + (uint64_t)dfd_len > (uint64_t)size ||
         dfd_off < 80u + (uint64_t)nlev * 24u))
        return TP_ERROR_INVALID_ARGUMENT;

    /* Key/value data: bounds-checked here, walked lazily by tp_ktx2_kv_lookup.
     * It is stored uncompressed even in a supercompressed file, so it aliases
     * the source for both schemes. */
    if (kvd_len != 0u) {
        if ((uint64_t)kvd_off + (uint64_t)kvd_len > (uint64_t)size ||
            kvd_off < 80u + (uint64_t)nlev * 24u)
            return TP_ERROR_INVALID_ARGUMENT;
        out->kvd = data + kvd_off;
        out->kvd_size = (size_t)kvd_len;
    }

    if (vk == 0u) {
        /* Basis UASTC also has vkFormat=UNDEFINED, but its wire representation
         * is not texcomp uni. Only accept our private UNSPECIFIED-model DFD;
         * treating KHR_DF_MODEL_UASTC as uni silently decodes corrupted pixels. */
        if (dfd_len < 44u ||
            data[(size_t)dfd_off + 12] != (uint8_t)TP_KDF_MODEL_UNSPECIFIED)
            return TP_ERROR_UNSUPPORTED;
        out->is_uni = 1;
        out->block_w = 4;
        out->block_h = 4;
        out->block_bytes = 16;
        /* A uni file carries no vkFormat, so the DFD is the only place the sRGB
         * and alpha facts live -- and they are exactly what a consumer needs to
         * pick an upload format and an alpha-capable transcode target. Parse the
         * two bytes we write; a DFD too short to hold them leaves the defaults
         * (linear, no alpha), which is what an absent DFD means anyway. */
        {
            uint8_t transfer = data[(size_t)dfd_off + 14];
            uint8_t channels = data[(size_t)dfd_off + 31] & 0x0fu;
            out->srgb = (transfer == (uint8_t)TP_KDF_TRANSFER_SRGB);
            out->has_alpha = (channels == (uint8_t)TP_KDF_CHANNEL_UNI_RGBA);
        }
    } else {
        tp_codec_desc d;
        tp_codec c;
        int srgb;
        if (!TP_OK(tp_vk_format_describe(vk, &d, &c, &srgb)))
            return TP_ERROR_UNSUPPORTED;
        out->codec = c;
        out->srgb = srgb;
        out->is_hdr = d.is_hdr;
        out->is_signed = d.is_signed;
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
            /* `owned` must be released here (the caller gets no image to free on
             * failure); the wrapper zeroes the half-filled level pointers. */
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

/* Every failure hands back a zeroed image -- no half-filled header, no level
 * pointers into a buffer that was freed on the way out. tp_ktx2_parse bails from
 * a dozen places, so the guarantee lives here rather than at each of them.
 * Note `out` must not already hold a loaded image: reading into a live one
 * overwrites its _owned pointer. Call tp_ktx2_image_free first. */
tp_result tp_ktx2_read_zstd(const uint8_t *data, size_t size,
                            const tir_allocator *a, tp_zstd_decompress_fn zdec,
                            void *user, tp_ktx2_image *out) {
    tp_result r;
    if (!data || !out) return TP_ERROR_INVALID_ARGUMENT;
    r = tp_ktx2_parse(data, size, a, zdec, user, out);
    if (!TP_OK(r)) memset(out, 0, sizeof(*out));
    return r;
}

/* Walk the KVD block: a sequence of {u32 keyAndValueByteLength, key NUL value,
 * padding to a 4-byte boundary}. Keys are unique and sorted in a valid file,
 * but a linear scan is both simpler and tolerant of files that are not. */
tp_result tp_ktx2_kv_lookup(const tp_ktx2_image *img, const char *key,
                            const uint8_t **value, size_t *value_size) {
    size_t pos = 0, klen;
    if (!img || !key || !value || !value_size) return TP_ERROR_INVALID_ARGUMENT;
    if (!img->kvd || img->kvd_size == 0u) return TP_ERROR_NOT_FOUND;
    klen = strlen(key);
    while (pos + 4u <= img->kvd_size) {
        uint32_t kv_len = tp_rd_u32(img->kvd + pos);
        const uint8_t *kv = img->kvd + pos + 4u;
        size_t i, keyz = 0;
        if (kv_len == 0u || kv_len > img->kvd_size - pos - 4u)
            return TP_ERROR_INVALID_ARGUMENT; /* entry runs past the block */
        /* The key is NUL-terminated inside the entry; without a NUL the entry is
         * malformed (and a strcmp here would read past it). */
        for (i = 0; i < kv_len; ++i)
            if (kv[i] == 0u) { keyz = i; break; }
        if (i == kv_len) return TP_ERROR_INVALID_ARGUMENT;
        if (keyz == klen && memcmp(kv, key, klen) == 0) {
            *value = kv + keyz + 1u;
            *value_size = kv_len - keyz - 1u;
            return TP_SUCCESS;
        }
        pos = tp_align_up(pos + 4u + kv_len, 4u);
    }
    return TP_ERROR_NOT_FOUND;
}

/* Validate (level, layer, face) and locate that slice's blocks. `texel_bytes` is
 * the size of one decoded output texel, so the caller's destination is checked
 * with the same 64-bit arithmetic in both the 8-bit and the float path. */
static tp_result tp_ktx2_slice(const tp_ktx2_image *img, int level, int layer,
                               int face, size_t texel_bytes, size_t out_size,
                               const uint8_t **out_blocks, uint32_t *out_w,
                               uint32_t *out_h) {
    uint32_t w, h;
    size_t img_bytes, slice;
    int nlayers;
    if (!img) return TP_ERROR_INVALID_ARGUMENT;
    if (level < 0 || level >= img->num_levels) return TP_ERROR_INVALID_ARGUMENT;
    nlayers = img->num_layers ? img->num_layers : 1; /* 0 = non-array */
    if (layer < 0 || layer >= nlayers) return TP_ERROR_INVALID_ARGUMENT;
    if (face < 0 || face >= img->num_faces) return TP_ERROR_INVALID_ARGUMENT;
    w = img->levels[level].width;
    h = img->levels[level].height;
    if (!w || !h) return TP_ERROR_INVALID_ARGUMENT;
    /* The surface size is computed in 64 bits first: on a 32-bit size_t it
     * wraps for a large level, which would let the out_size check pass. */
    if ((uint64_t)w * (uint64_t)h * (uint64_t)texel_bytes > (uint64_t)(size_t)-1)
        return TP_ERROR_UNSUPPORTED;
    if (out_size < (size_t)w * (size_t)h * texel_bytes)
        return TP_ERROR_INVALID_ARGUMENT;

    /* A level holds its slices in KTX2 order (layer, face), each one a tightly
     * packed block image of the level's dimensions. */
    img_bytes = (size_t)(((uint64_t)w + (uint64_t)img->block_w - 1u) /
                         (uint64_t)img->block_w) *
                (size_t)(((uint64_t)h + (uint64_t)img->block_h - 1u) /
                         (uint64_t)img->block_h) *
                (size_t)img->block_bytes;
    slice = ((size_t)layer * (size_t)img->num_faces + (size_t)face) * img_bytes;
    /* The reader's level-size check covers all slices, but a hand-built
     * tp_ktx2_image (or a level whose declared size we merely lower-bounded)
     * could still come up short -- decoders read img_bytes without a length. */
    if (img->levels[level].size < slice + img_bytes)
        return TP_ERROR_INVALID_ARGUMENT;
    *out_blocks = img->levels[level].data + slice;
    *out_w = w;
    *out_h = h;
    return TP_SUCCESS;
}

tp_result tp_ktx2_decode_level_rgbaf(const tp_ktx2_image *img, int level,
                                     float *out_rgba, size_t out_size) {
    return tp_ktx2_decode_slice_rgbaf(img, level, 0, 0, out_rgba, out_size);
}

tp_result tp_ktx2_decode_slice_rgbaf(const tp_ktx2_image *img, int level,
                                     int layer, int face, float *out_rgba,
                                     size_t out_size) {
    uint32_t w, h;
    const uint8_t *blocks;
    tp_result r;
    if (!img || !out_rgba) return TP_ERROR_INVALID_ARGUMENT;
    r = tp_ktx2_slice(img, level, layer, face, 4u * sizeof(float), out_size,
                      &blocks, &w, &h);
    if (!TP_OK(r)) return r;
    if (img->is_uni) return TP_ERROR_UNSUPPORTED;
    switch (img->codec) {
    case TP_CODEC_BC6H:
        return tp_from_tc(tc_bc6h_decompress_rgbaf(blocks, w, h, img->is_signed,
                                                   (size_t)w * 4u * sizeof(float),
                                                   out_rgba, out_size));
    case TP_CODEC_ASTC_HDR:
        return tp_from_tc(tc_astc_hdr_decompress_rgbaf(
            blocks, w, h, (uint32_t)img->block_w, (uint32_t)img->block_h,
            (size_t)w * 4u * sizeof(float), out_rgba, out_size));
    default:
        /* The LDR codecs stay on the RGBA8 path rather than being widened. */
        return TP_ERROR_UNSUPPORTED;
    }
}

tp_result tp_ktx2_decode_level_rgba8(const tp_ktx2_image *img, int level,
                                     uint8_t *out_rgba, size_t out_size) {
    return tp_ktx2_decode_slice_rgba8(img, level, 0, 0, out_rgba, out_size);
}

tp_result tp_ktx2_decode_slice_rgba8(const tp_ktx2_image *img, int level,
                                     int layer, int face, uint8_t *out_rgba,
                                     size_t out_size) {
    uint32_t w, h;
    const uint8_t *blocks;
    tp_result r;
    if (!img || !out_rgba) return TP_ERROR_INVALID_ARGUMENT;
    r = tp_ktx2_slice(img, level, layer, face, 4u, out_size, &blocks, &w, &h);
    if (!TP_OK(r)) return r;

    if (img->is_uni)
        return tp_from_tc(tc_uni_decompress_rgba8(blocks, w, h, (size_t)w * 4u,
                                                  out_rgba, out_size));

    switch (img->codec) {
    case TP_CODEC_BC7:
        return tp_from_tc(tc_bc7_decompress_rgba8(blocks, w, h, (size_t)w * 4u,
                                                  out_rgba, out_size));
    case TP_CODEC_BC1:
        return tp_from_tc(tc_bc1_decompress_rgba8(blocks, w, h, (size_t)w * 4u,
                                                  out_rgba, out_size));
    case TP_CODEC_BC3:
        return tp_from_tc(tc_bc3_decompress_rgba8(blocks, w, h, (size_t)w * 4u,
                                                  out_rgba, out_size));
    case TP_CODEC_BC5:
        return tp_from_tc(tc_bc5_decompress_rgba8(blocks, w, h, img->is_signed,
                                                  (size_t)w * 4u, out_rgba,
                                                  out_size));
    case TP_CODEC_ETC2_RGB:
    case TP_CODEC_ETC2_RGBA:
        return tp_from_tc(tc_etc2_decompress_rgba8(
            blocks, w, h, img->codec == TP_CODEC_ETC2_RGBA, (size_t)w * 4u,
            out_rgba, out_size));
    case TP_CODEC_EAC_R11:
    case TP_CODEC_EAC_RG11:
        return tp_from_tc(tc_eac_decompress_rgba8(
            blocks, w, h, img->codec == TP_CODEC_EAC_RG11, (size_t)w * 4u,
            out_rgba, out_size));
    case TP_CODEC_ASTC:
        return tp_from_tc(tc_astc_decompress_rgba8(
            blocks, w, h, (uint32_t)img->block_w, (uint32_t)img->block_h,
            out_rgba, out_size));
    default:
        /* BC6H is HDR: decode it with tp_ktx2_decode_slice_rgbaf instead.
         * ASTC HDR has no decoder yet. */
        return TP_ERROR_UNSUPPORTED;
    }
}

/* ---- uni KTX2 writer: private carrier or standard ASTC 4x4 ---- */

#define TP_UNI_FLAGS_ALL (TP_UNI_SRGB | TP_UNI_ALPHA | TP_UNI_ASTC_KTX2)
#define TP_VK_ASTC_4X4_UNORM_BLOCK 157u
#define TP_VK_ASTC_4X4_SRGB_BLOCK 158u

/* Private uni descriptor (44 bytes). It deliberately uses colorModel =
 * UNSPECIFIED: uni is not the Basis UASTC wire representation, and labelling it
 * UASTC causes standards-compliant consumers to silently decode wrong pixels. */
static void tp_write_uni_dfd(uint8_t *p, uint32_t flags) {
    memset(p, 0, 44);
    tp_wr_u32(p + 0, 44u);                 /* dfdTotalSize */
    tp_wr_u32(p + 8, 2u | (40u << 16));    /* version | descriptorBlockSize */
    p[12] = (uint8_t)TP_KDF_MODEL_UNSPECIFIED;
    p[13] = (uint8_t)((flags & TP_UNI_SRGB) ? TP_KDF_PRIMARIES_BT709
                                            : TP_KDF_PRIMARIES_UNSPECIFIED);
    p[14] = (uint8_t)((flags & TP_UNI_SRGB) ? TP_KDF_TRANSFER_SRGB
                                            : TP_KDF_TRANSFER_LINEAR);
    p[16] = 3u; p[17] = 3u;                /* texelBlockDimension = 4x4 */
    /* The pre-deflation block size, kept as-is even under supercompression:
     * KTX2 2.0.4 dropped the old "bytesPlane must be 0 if supercompressed" rule
     * and now requires the real value (0 merely warns as deprecated). */
    p[20] = 16u;                           /* bytesPlane0 */
    p[30] = 127u;                          /* sample bitLength (128 bits) */
    p[31] = (uint8_t)((flags & TP_UNI_ALPHA) ? TP_KDF_CHANNEL_UNI_RGBA
                                             : TP_KDF_CHANNEL_UNI_RGB);
    tp_wr_u32(p + 40, 0xffffffffu);        /* sampleUpper */
}

/* Tightly-packed 4x4x16B payload of uni level `l`, sized off the base dimensions
 * the same way the reader derives them (width >> l, clamped to 1) rather than
 * off level_w[]/level_h[]. Computed in uint64 like the reader's
 * tp_ktx2_level_expected: at the largest dimensions TP_KTX2_MAX_DIM admits the
 * product is exactly 2^32, which a 32-bit size_t (the wasm build) would wrap to
 * 0 -- and a 0 here would make the caller's size check accept a zero-length
 * level. Such a level cannot fit in a 32-bit size_t anyway, so on those targets
 * no uni_sizes[l] can match and the write is rejected, which is the point. */
static uint64_t tp_uni_level_expected(uint32_t w0, uint32_t h0, int l) {
    uint32_t w, h;
    if (!w0 || !h0) return 0;
    /* Same clamp-to-1 mip rule the rest of the pipeline uses. */
    w = (uint32_t)tp_level_dim((int)w0, l);
    h = (uint32_t)tp_level_dim((int)h0, l);
    return (uint64_t)((w + 3u) / 4u) * (uint64_t)((h + 3u) / 4u) * 16u;
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

/* Everything both uni writers must reject. Each rule exists because the reader
 * (or the KTX2 spec) refuses the result, so writing one would only produce a
 * file that cannot be loaded back -- a silent failure at asset-build time that
 * surfaces much later. Shared so the two writers cannot drift apart. */
static tp_result tp_uni_check(const uint8_t *const *uni_levels,
                              const size_t *uni_sizes, uint32_t base_w,
                              uint32_t base_h, int num_levels, uint32_t flags) {
    int l;
    if (!uni_levels || !uni_sizes) return TP_ERROR_INVALID_ARGUMENT;
    if (num_levels < 1 || num_levels > TP_KTX2_MAX_LEVELS)
        return TP_ERROR_INVALID_ARGUMENT;
    if (flags & ~TP_UNI_FLAGS_ALL) return TP_ERROR_INVALID_ARGUMENT;
    if (!tp_ktx2_dims_ok(base_w, base_h)) return TP_ERROR_INVALID_ARGUMENT;
    /* levelCount past the pyramid the base dimensions imply is invalid KTX2:
     * the surplus levels all clamp to 1x1, and a loader that trusts levelCount
     * would bind mips that do not exist. */
    if (num_levels > tp_level_count((int)base_w, (int)base_h, 0))
        return TP_ERROR_INVALID_ARGUMENT;
    /* Level sizes must be exactly the block payload the reader derives from the
     * base dimensions -- the scheme-2 reader pins uncompressedByteLength to it,
     * and the scheme-0 reader rejects anything shorter as truncated. */
    for (l = 0; l < num_levels; ++l)
        if (!uni_levels[l] ||
            (uint64_t)uni_sizes[l] != tp_uni_level_expected(base_w, base_h, l))
            return TP_ERROR_INVALID_ARGUMENT;
    return TP_SUCCESS;
}

/* Header + level index + DFD, i.e. everything before the level payloads. The
 * two writers differ only in `scheme` and in the byteLength column: raw levels
 * store their own size there, supercompressed ones the compressed size. */
static void tp_ktx2_uni_emit(uint8_t *buf, uint32_t base_w, uint32_t base_h,
                             int num_levels, uint32_t scheme, uint32_t flags,
                             const uint64_t *loff, const size_t *byte_len,
                             const size_t *uncompressed_len) {
    size_t dfd_off = 80u + (size_t)num_levels * 24u;
    int astc_ktx2 = (flags & TP_UNI_ASTC_KTX2) != 0u;
    int l;
    memcpy(buf, tp_ktx2_id, 12);
    tp_wr_u32(buf + 12,
              astc_ktx2 ? ((flags & TP_UNI_SRGB)
                                ? TP_VK_ASTC_4X4_SRGB_BLOCK
                                : TP_VK_ASTC_4X4_UNORM_BLOCK)
                        : 0u);
    tp_wr_u32(buf + 16, 1u);        /* typeSize                   */
    tp_wr_u32(buf + 20, base_w);
    tp_wr_u32(buf + 24, base_h);
    tp_wr_u32(buf + 28, 0u);        /* pixelDepth                 */
    tp_wr_u32(buf + 32, 0u);        /* layerCount                 */
    tp_wr_u32(buf + 36, 1u);        /* faceCount                  */
    tp_wr_u32(buf + 40, (uint32_t)num_levels);
    tp_wr_u32(buf + 44, scheme);
    tp_wr_u32(buf + 48, (uint32_t)dfd_off);
    tp_wr_u32(buf + 52, 44u);
    tp_wr_u32(buf + 56, 0u);        /* kvdByteOffset              */
    tp_wr_u32(buf + 60, 0u);        /* kvdByteLength              */
    tp_wr_u64(buf + 64, 0u);        /* sgdByteOffset              */
    tp_wr_u64(buf + 72, 0u);        /* sgdByteLength              */
    for (l = 0; l < num_levels; ++l) {
        uint8_t *e = buf + 80u + (size_t)l * 24u;
        tp_wr_u64(e + 0, loff[l]);
        tp_wr_u64(e + 8, (uint64_t)byte_len[l]);
        tp_wr_u64(e + 16, (uint64_t)uncompressed_len[l]);
    }
    if (astc_ktx2) {
        tp_codec_desc d;
        memset(&d, 0, sizeof(d));
        d.block_w = 4;
        d.block_h = 4;
        d.block_bytes = 16;
        tp_write_dfd(buf + dfd_off, &d, TP_CODEC_ASTC,
                     (flags & TP_UNI_SRGB) != 0u);
    } else {
        tp_write_uni_dfd(buf + dfd_off, flags);
    }
}

tp_result tp_ktx2_write_uni_ex(const uint8_t *const *uni_levels,
                               const size_t *uni_sizes, uint32_t base_w,
                               uint32_t base_h, int num_levels, uint32_t flags,
                               uint8_t *out, size_t out_size, size_t *written) {
    /* Zeroed because tp_ktx2_uni_layout fills only [0, num_levels) and the
     * compiler cannot see that across the call into tp_ktx2_uni_emit. */
    uint64_t loff[TP_KTX2_MAX_LEVELS] = {0};
    size_t need;
    tp_result r;
    int l;
    if (!out) return TP_ERROR_INVALID_ARGUMENT;
    r = tp_uni_check(uni_levels, uni_sizes, base_w, base_h, num_levels, flags);
    if (!TP_OK(r)) return r;
    need = tp_ktx2_uni_layout(uni_sizes, num_levels, loff);
    if (need == 0u) return TP_ERROR_INVALID_ARGUMENT; /* layout overflowed */
    if (out_size < need) return TP_ERROR_INVALID_ARGUMENT;
    tp_ktx2_uni_emit(out, base_w, base_h, num_levels, 0u, flags, loff, uni_sizes,
                     uni_sizes);
    /* Emit covered everything up to the level data, and the copies below cover
     * the levels themselves. What is left is this layout's one gap: unlike the
     * supercompressed one, it aligns each level to 16, and mipPadding must be
     * zero. Zero just those gaps rather than the whole output, which for a large
     * chain is an extra full pass over tens of MB. Levels run smallest-first, so
     * walking l descending walks the file in address order. */
    {
        size_t cursor = 80u + (size_t)num_levels * 24u + 44u;
        for (l = num_levels - 1; l >= 0; --l) {
            memset(out + cursor, 0, (size_t)loff[l] - cursor);
            memcpy(out + (size_t)loff[l], uni_levels[l], uni_sizes[l]);
            cursor = (size_t)loff[l] + uni_sizes[l];
        }
    }
    if (written) *written = need;
    return TP_SUCCESS;
}

/* Pre-flags compatibility form. Only level_w[0]/level_h[0] were ever read, and
 * flags = 0 reproduces the DFD this used to emit (linear, no alpha). */
tp_result tp_ktx2_write_uni(const uint8_t *const *uni_levels,
                            const size_t *uni_sizes, const uint32_t *level_w,
                            const uint32_t *level_h, int num_levels,
                            uint8_t *out, size_t out_size, size_t *written) {
    if (!level_w || !level_h) return TP_ERROR_INVALID_ARGUMENT;
    return tp_ktx2_write_uni_ex(uni_levels, uni_sizes, level_w[0], level_h[0],
                                num_levels, 0u, out, out_size, written);
}

tp_result tp_ktx2_write_uni_zstd(const tir_allocator *a, tp_zstd_bound_fn zbound,
                                 tp_zstd_compress_fn zenc, void *user,
                                 const uint8_t *const *uni_levels,
                                 const size_t *uni_sizes, uint32_t base_w,
                                 uint32_t base_h, int num_levels, uint32_t flags,
                                 uint8_t **out, size_t *out_size) {
    /* Zeroed for the same reason as in tp_ktx2_write_uni_ex: the compiler cannot
     * see that the loop below fills exactly [0, num_levels) before both arrays
     * are handed to tp_ktx2_uni_emit. */
    size_t clen[TP_KTX2_MAX_LEVELS] = {0};
    uint64_t loff[TP_KTX2_MAX_LEVELS] = {0};
    const size_t smax = (size_t)-1;
    size_t head, cap, cursor;
    uint8_t *buf = NULL, *fit = NULL;
    tp_result r;
    int l;

    if (!out || !out_size) return TP_ERROR_INVALID_ARGUMENT;
    /* Cleared before every other rejection, including the missing-callback one:
     * the contract is that a failed call leaves nothing for the caller to free. */
    *out = NULL;
    *out_size = 0;
    if (!zbound || !zenc) return TP_ERROR_INVALID_ARGUMENT;
    r = tp_uni_check(uni_levels, uni_sizes, base_w, base_h, num_levels, flags);
    if (!TP_OK(r)) return r;

    /* Layout: header + level index + DFD, then the compressed levels packed
     * smallest-first. Supercompressed levels carry no mip padding -- KTX2 sets
     * required_alignment = 1 when supercompressionScheme != 0.
     *
     * The compressed sizes are not known up front, so compress into a worst-case
     * buffer (every level incompressible) at a running cursor, then copy the
     * result down into an exact-sized one. Compressing in place rather than into
     * per-level scratch keeps this to two allocations instead of num_levels + 1,
     * and the caller is handed a buffer the size of the actual file -- handing
     * back the worst-case block would mean holding the whole uncompressed
     * pyramid alive for a file that is typically a fraction of it. */
    head = 80u + (size_t)num_levels * 24u + 44u;
    cap = head;
    for (l = 0; l < num_levels; ++l) {
        size_t bound = zbound(user, uni_sizes[l]);
        if (bound == 0) return TP_ERROR_UNSUPPORTED;
        if (bound > smax - cap) return TP_ERROR_INVALID_ARGUMENT;
        cap += bound;
    }
    buf = (uint8_t *)tp_alloc(a, cap);
    if (!buf) return TP_ERROR_OUT_OF_MEMORY;

    /* Compress each level independently, so a reader can inflate one at a time.
     * Smallest-first, so the cursor walks the levels in the order they are
     * stored. Each level still gets at least its own zbound of room: what is
     * left of `cap` here is the sum of the bounds of this level and every level
     * not yet written. */
    cursor = head;
    for (l = num_levels - 1; l >= 0; --l) {
        loff[l] = cursor;
        clen[l] = zenc(user, buf + cursor, cap - cursor, uni_levels[l],
                       uni_sizes[l]);
        if (clen[l] == 0 || clen[l] > cap - cursor) {
            r = TP_ERROR_UNSUPPORTED;
            goto done;
        }
        cursor += clen[l];
    }

    /* Nothing below `head` has been touched yet, and the levels wrote every byte
     * from `head` to `cursor` -- so no memset is needed anywhere. */
    tp_ktx2_uni_emit(buf, base_w, base_h, num_levels, 2u, flags, loff, clen,
                     uni_sizes);

    if (cursor < cap) {
        fit = (uint8_t *)tp_alloc(a, cursor);
        if (!fit) { r = TP_ERROR_OUT_OF_MEMORY; goto done; }
        memcpy(fit, buf, cursor);
        tp_dealloc(a, buf);
        buf = fit;
    }
    *out = buf;
    *out_size = cursor;
    return TP_SUCCESS;

done:
    tp_dealloc(a, buf);
    return r;
}

/* ---- KTX2 texture arrays (layerCount) ---- */

/* As tp_ktx2_layout, but every level holds num_layers copies. Returns 0 if the
 * layout overflows size_t -- the multiply by num_layers makes that reachable on
 * a 32-bit target long before the 64-bit one, and an under-reported size here is
 * what the payload memcpy would then run off the end of. */
static size_t tp_ktx2_array_layout(const tp_blocks *layers, int num_layers,
                                   const tp_codec_desc *d, uint64_t *level_off,
                                   uint64_t *level_len) {
    size_t idx_bytes = (size_t)layers[0].num_levels * 24u;
    size_t align = (size_t)d->block_bytes;
    size_t cursor = 80u + idx_bytes + TP_DFD_TOTAL;
    int ll;
    if (align < 4u) align = 4u;
    for (ll = layers[0].num_levels - 1; ll >= 0; --ll) {
        size_t one = tp_ktx2_level_len(&layers[0], ll);
        size_t len;
        if (one == TP_SIZE_OVF) return 0;
        if (one != 0u && (size_t)num_layers > TP_SIZE_OVF / one) return 0;
        len = (size_t)num_layers * one;
        if (cursor > TP_SIZE_OVF - (align - 1u)) return 0;
        cursor = tp_align_up(cursor, align);
        if (len > TP_SIZE_OVF - cursor) return 0;
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
    if (!tp_blocks_shape_ok(&layers[0], opt)) return 0;
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
    /* Checks layers[0]'s shape and its codec against opt's, before anything
     * reads blk[0]. */
    if (!tp_blocks_shape_ok(&layers[0], opt)) return TP_ERROR_UNSUPPORTED;
    nlev = layers[0].num_levels;
    nface = layers[0].num_faces;
    /* The reader refuses layerCount past this, so writing more would only make a
     * file that cannot be loaded back. */
    if (num_layers > TP_KTX2_MAX_LAYERS) return TP_ERROR_UNSUPPORTED;
    for (li = 1; li < num_layers; ++li) {
        int k;
        if (!layers[li].blk || layers[li].num_levels != nlev ||
            layers[li].num_faces != nface || layers[li].codec != layers[0].codec)
            return TP_ERROR_INVALID_ARGUMENT;
        /* Every level's byte length is computed from layers[0] and multiplied by
         * num_layers, but the payload loop below copies each layer's own
         * blk[].size. A layer that disagrees on dimensions therefore memcpy's
         * past the end of `out`. Require the layers to be block-for-block
         * identical in shape -- which is what a KTX2 array is. */
        for (k = 0; k < nface * nlev; ++k)
            if (layers[li].blk[k].size != layers[0].blk[k].size ||
                layers[li].blk[k].width != layers[0].blk[k].width ||
                layers[li].blk[k].height != layers[0].blk[k].height)
                return TP_ERROR_INVALID_ARGUMENT;
    }
    if (!TP_OK(tp_codec_describe(opt->codec, opt, &d)) || d.vk_format == 0u)
        return TP_ERROR_UNSUPPORTED;
    need = tp_ktx2_array_layout(layers, num_layers, &d, level_off, level_len);
    if (need == 0u) return TP_ERROR_INVALID_ARGUMENT; /* layout overflowed */
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
