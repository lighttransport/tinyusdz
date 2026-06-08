/*
 * TinyEXR - deep scanline images (read).
 *
 * Each deep scanline chunk stores a compressed per-pixel offset table
 * (cumulative sample counts, row-major within the block) followed by compressed
 * channel-planar sample data. We expand into per-pixel counts and per-channel
 * contiguous sample arrays.
 *
 * Copyright (c) 2014-2026 Syoyo Fujita and TinyEXR authors
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "exr_internal.h"


/* Decompress deep payload (offset table or sample data) into dst[unpacked]. */
static exr_result deep_decompress(const exr_allocator *a, exr_compression comp,
                                  const uint8_t *src, size_t src_size,
                                  uint8_t *dst, size_t unpacked) {
    if (unpacked == 0) return EXR_SUCCESS;
    if (src_size == unpacked) { /* stored uncompressed */
        memcpy(dst, src, unpacked);
        return EXR_SUCCESS;
    }
    switch (comp) {
    case EXR_COMPRESSION_NONE:
    case EXR_COMPRESSION_HTJ2K32:
    case EXR_COMPRESSION_HTJ2K256:
        return EXR_ERROR_CORRUPT;
    case EXR_COMPRESSION_RLE:
        return exr_rle_decompress(a, src, src_size, dst, unpacked);
    case EXR_COMPRESSION_ZIP:
    case EXR_COMPRESSION_ZIPS:
        return exr_zip_decompress(a, src, src_size, dst, unpacked);
    default:
        return EXR_ERROR_UNSUPPORTED; /* deep PIZ/etc. not supported */
    }
}

exr_result exr_read_deep_scanline_part(exr_reader *r, exr_int_part *p,
                                       int32_t part_idx, exr_part *out) {
    const exr_allocator *a = &r->alloc;
    const exr_header *h = &p->header;
    int width = p->width, height = p->height, nch = h->num_channels;
    int ymin = h->data_window.min_y, ymax = h->data_window.max_y;
    int lpb = exr_lines_per_block(h->compression);
    size_t sample_size = 0;
    int c;
    uint32_t ci;
    int32_t *otab = NULL; /* per-chunk offset table scratch */
    uint8_t *sbuf = NULL;
    size_t otab_cap = 0, sbuf_cap = 0;
    uint64_t total = 0, *run = NULL; /* running per-channel sample offset */
    exr_result rc = EXR_SUCCESS;

    for (c = 0; c < nch; ++c)
        sample_size += exr_pixel_size(h->channels[c].pixel_type);
    if (sample_size == 0 || width <= 0 || height <= 0)
        return EXR_ERROR_CORRUPT;

    out->is_deep = 1;
    {
        size_t npix;
        if (exr_mul_ovf((size_t)width, (size_t)height, &npix))
            return EXR_ERROR_CORRUPT;
        out->deep_sample_counts =
            (int32_t *)exr_calloc(a, npix, sizeof(int32_t));
    }
    out->deep_images = (void **)exr_calloc(a, nch ? (size_t)nch : 1, sizeof(void *));
    run = (uint64_t *)exr_calloc(a, nch ? (size_t)nch : 1, sizeof(uint64_t));
    if (!out->deep_sample_counts || !out->deep_images || !run) {
        rc = EXR_ERROR_OUT_OF_MEMORY;
        goto done;
    }

    /* Pass 1: read every chunk's offset table -> per-pixel counts + total. */
    for (ci = 0; ci < p->num_chunks; ++ci) {
        uint64_t off = p->offsets[ci];
        const uint8_t *hdr, *packed;
        size_t hsz = r->is_multipart ? 32 : 28;
        int32_t y0, nlines, r2, x;
        uint64_t pos, pots, psds, usds, prev;
        size_t need;

        rc = exr_reader_fetch(r, off, hsz, NULL, &hdr);
        if (!EXR_OK(rc)) goto done;
        if (r->is_multipart) {
            if (exr_rd_i32(hdr) != part_idx) { rc = EXR_ERROR_CORRUPT; goto done; }
            hdr += 4;
        }
        y0 = exr_rd_i32(hdr);
        pots = exr_rd_u64(hdr + 4);
        psds = exr_rd_u64(hdr + 12);
        usds = exr_rd_u64(hdr + 20);
        (void)psds;
        (void)usds;
        if (y0 < ymin || y0 > ymax) { rc = EXR_ERROR_CORRUPT; goto done; }
        nlines = lpb;
        if (y0 + nlines - 1 > ymax) nlines = ymax - y0 + 1;

        need = (size_t)width * nlines;
        if (need > otab_cap) {
            exr_free(a, otab);
            otab = (int32_t *)exr_malloc(a, need * sizeof(int32_t));
            if (!otab) { rc = EXR_ERROR_OUT_OF_MEMORY; goto done; }
            otab_cap = need;
        }
        pos = off + hsz;
        rc = exr_reader_fetch(r, pos, (size_t)pots, NULL, &packed);
        if (!EXR_OK(rc)) goto done;
        rc = deep_decompress(a, h->compression, packed, (size_t)pots,
                             (uint8_t *)otab, need * sizeof(int32_t));
        if (!EXR_OK(rc)) goto done;

        prev = 0;
        for (r2 = 0; r2 < nlines; ++r2) {
            int row = y0 - ymin + r2;
            for (x = 0; x < width; ++x) {
                int32_t cum = otab[r2 * width + x];
                int64_t cnt = (int64_t)cum - (int64_t)prev;
                if (cnt < 0) { rc = EXR_ERROR_CORRUPT; goto done; }
                out->deep_sample_counts[(size_t)row * width + x] = (int32_t)cnt;
                prev = (uint64_t)cum;
            }
        }
        total += prev;
    }

    out->deep_total_samples = total;
    for (c = 0; c < nch; ++c) {
        size_t bytes;
        if (exr_mul_ovf((size_t)total, exr_pixel_size(h->channels[c].pixel_type),
                        &bytes)) {
            rc = EXR_ERROR_CORRUPT;
            goto done;
        }
        out->deep_images[c] = exr_calloc(a, bytes ? bytes : 1, 1);
        if (!out->deep_images[c]) { rc = EXR_ERROR_OUT_OF_MEMORY; goto done; }
    }

    /* Pass 2: read sample data, scatter channel-planar into deep_images. */
    for (ci = 0; ci < p->num_chunks; ++ci) {
        uint64_t off = p->offsets[ci];
        const uint8_t *hdr, *packed;
        size_t hsz = r->is_multipart ? 32 : 28;
        int32_t y0, nlines, r2, x;
        uint64_t pots, psds, usds, pos, chunk_total = 0;
        size_t soff;

        rc = exr_reader_fetch(r, off, hsz, NULL, &hdr);
        if (!EXR_OK(rc)) goto done;
        if (r->is_multipart) hdr += 4;
        y0 = exr_rd_i32(hdr);
        pots = exr_rd_u64(hdr + 4);
        psds = exr_rd_u64(hdr + 12);
        usds = exr_rd_u64(hdr + 20);
        nlines = lpb;
        if (y0 + nlines - 1 > ymax) nlines = ymax - y0 + 1;
        for (r2 = 0; r2 < nlines; ++r2) {
            int row = y0 - ymin + r2;
            for (x = 0; x < width; ++x)
                chunk_total += (uint64_t)out->deep_sample_counts[(size_t)row * width + x];
        }
        if (usds != chunk_total * sample_size) { rc = EXR_ERROR_CORRUPT; goto done; }

        if ((size_t)usds > sbuf_cap) {
            exr_free(a, sbuf);
            sbuf = (uint8_t *)exr_malloc(a, (size_t)usds ? (size_t)usds : 1);
            if (!sbuf) { rc = EXR_ERROR_OUT_OF_MEMORY; goto done; }
            sbuf_cap = (size_t)usds;
        }
        pos = off + hsz + pots;
        rc = exr_reader_fetch(r, pos, (size_t)psds, NULL, &packed);
        if (!EXR_OK(rc)) goto done;
        rc = deep_decompress(a, h->compression, packed, (size_t)psds, sbuf,
                             (size_t)usds);
        if (!EXR_OK(rc)) goto done;

        soff = 0;
        for (c = 0; c < nch; ++c) {
            size_t ps = exr_pixel_size(h->channels[c].pixel_type);
            size_t cb = (size_t)chunk_total * ps;
            memcpy((uint8_t *)out->deep_images[c] + run[c] * ps, sbuf + soff, cb);
            soff += cb;
            run[c] += chunk_total;
        }
    }

done:
    exr_free(a, otab);
    exr_free(a, sbuf);
    exr_free(a, run);
    return rc;
}

/* ---- deep block streaming decode (one chunk, block-local) ----------------- */

static size_t deep_chunk_hdr_size(const exr_reader *r, int is_tiled) {
    if (is_tiled) return r->is_multipart ? 44 : 40;
    return r->is_multipart ? 32 : 28;
}

exr_result exr_deep_decode_counts(exr_reader *r, exr_int_part *p,
                                  int32_t part_idx, uint32_t idx, int bw, int bh,
                                  int is_tiled, int32_t *counts) {
    const exr_allocator *a = &r->alloc;
    const exr_header *h = &p->header;
    uint64_t off, pots;
    const uint8_t *hdr, *packed;
    size_t hsz, need, i;
    int32_t *otab;
    int64_t prev = 0;
    exr_result rc;

    if (idx >= p->num_chunks) return EXR_ERROR_INVALID_ARGUMENT;
    if (bw < 0 || bh < 0) return EXR_ERROR_CORRUPT;
    off = p->offsets[idx];
    hsz = deep_chunk_hdr_size(r, is_tiled);
    rc = exr_reader_fetch(r, off, hsz, NULL, &hdr);
    if (!EXR_OK(rc)) return rc;
    if (r->is_multipart) {
        if (exr_rd_i32(hdr) != part_idx) return EXR_ERROR_CORRUPT;
        hdr += 4;
    }
    pots = is_tiled ? exr_rd_u64(hdr + 16) : exr_rd_u64(hdr + 4);

    need = (size_t)bw * (size_t)bh;
    otab = (int32_t *)exr_malloc(a, (need ? need : 1) * sizeof(int32_t));
    if (!otab) return EXR_ERROR_OUT_OF_MEMORY;
    rc = exr_reader_fetch(r, off + hsz, (size_t)pots, NULL, &packed);
    if (!EXR_OK(rc)) { exr_free(a, otab); return rc; }
    rc = deep_decompress(a, h->compression, packed, (size_t)pots,
                         (uint8_t *)otab, need * sizeof(int32_t));
    if (!EXR_OK(rc)) { exr_free(a, otab); return rc; }
    for (i = 0; i < need; ++i) {
        int64_t cum = otab[i], cnt = cum - prev;
        if (cnt < 0) { exr_free(a, otab); return EXR_ERROR_CORRUPT; }
        counts[i] = (int32_t)cnt;
        prev = cum;
    }
    exr_free(a, otab);
    return EXR_SUCCESS;
}

exr_result exr_deep_decode_samples(exr_reader *r, exr_int_part *p,
                                   int32_t part_idx, uint32_t idx, int bw, int bh,
                                   int is_tiled, void *const *chan_dst) {
    const exr_allocator *a = &r->alloc;
    const exr_header *h = &p->header;
    int nch = h->num_channels, c;
    uint64_t off, pots, psds, usds, total = 0;
    const uint8_t *hdr, *packed;
    size_t hsz, need, sample_size = 0, soff;
    int32_t *otab = NULL;
    uint8_t *sbuf = NULL;
    exr_result rc;

    if (idx >= p->num_chunks) return EXR_ERROR_INVALID_ARGUMENT;
    if (bw < 0 || bh < 0) return EXR_ERROR_CORRUPT;
    for (c = 0; c < nch; ++c)
        sample_size += exr_pixel_size(h->channels[c].pixel_type);
    off = p->offsets[idx];
    hsz = deep_chunk_hdr_size(r, is_tiled);
    rc = exr_reader_fetch(r, off, hsz, NULL, &hdr);
    if (!EXR_OK(rc)) return rc;
    if (r->is_multipart) {
        if (exr_rd_i32(hdr) != part_idx) return EXR_ERROR_CORRUPT;
        hdr += 4;
    }
    if (is_tiled) {
        pots = exr_rd_u64(hdr + 16);
        psds = exr_rd_u64(hdr + 24);
        usds = exr_rd_u64(hdr + 32);
    } else {
        pots = exr_rd_u64(hdr + 4);
        psds = exr_rd_u64(hdr + 12);
        usds = exr_rd_u64(hdr + 20);
    }

    /* the offset table's last cumulative entry is the block's total sample count */
    need = (size_t)bw * (size_t)bh;
    otab = (int32_t *)exr_malloc(a, (need ? need : 1) * sizeof(int32_t));
    if (!otab) return EXR_ERROR_OUT_OF_MEMORY;
    rc = exr_reader_fetch(r, off + hsz, (size_t)pots, NULL, &packed);
    if (!EXR_OK(rc)) goto done;
    rc = deep_decompress(a, h->compression, packed, (size_t)pots,
                         (uint8_t *)otab, need * sizeof(int32_t));
    if (!EXR_OK(rc)) goto done;
    if (need > 0) {
        if (otab[need - 1] < 0) { rc = EXR_ERROR_CORRUPT; goto done; }
        total = (uint64_t)otab[need - 1];
    }
    if (usds != total * sample_size) { rc = EXR_ERROR_CORRUPT; goto done; }

    sbuf = (uint8_t *)exr_malloc(a, (size_t)usds ? (size_t)usds : 1);
    if (!sbuf) { rc = EXR_ERROR_OUT_OF_MEMORY; goto done; }
    rc = exr_reader_fetch(r, off + hsz + pots, (size_t)psds, NULL, &packed);
    if (!EXR_OK(rc)) goto done;
    rc = deep_decompress(a, h->compression, packed, (size_t)psds, sbuf,
                         (size_t)usds);
    if (!EXR_OK(rc)) goto done;
    soff = 0;
    for (c = 0; c < nch; ++c) {
        size_t ps = exr_pixel_size(h->channels[c].pixel_type);
        size_t cb = (size_t)total * ps;
        if (chan_dst[c] && cb) memcpy(chan_dst[c], sbuf + soff, cb);
        soff += cb;
    }

done:
    exr_free(a, otab);
    exr_free(a, sbuf);
    return rc;
}

/* ---- deep scanline write -------------------------------------------------- */

static exr_result deep_compress(const exr_allocator *a, exr_compression comp,
                                const uint8_t *src, size_t n, uint8_t **out,
                                size_t *out_size) {
    switch (comp) {
    case EXR_COMPRESSION_NONE:
    case EXR_COMPRESSION_HTJ2K32:
    case EXR_COMPRESSION_HTJ2K256: {
        *out = (uint8_t *)exr_malloc(a, n ? n : 1);
        if (!*out) return EXR_ERROR_OUT_OF_MEMORY;
        memcpy(*out, src, n);
        *out_size = n;
        return EXR_SUCCESS;
    }
    case EXR_COMPRESSION_RLE:
        return exr_rle_compress(a, src, n, out, out_size);
    case EXR_COMPRESSION_ZIP:
    case EXR_COMPRESSION_ZIPS:
        return exr_zip_compress(a, src, n, out, out_size);
    default:
        return EXR_ERROR_UNSUPPORTED;
    }
}

exr_result exr_deep_encode_block(const exr_allocator *a, exr_compression comp,
                                 const exr_part *part, const uint64_t *prefix,
                                 int y0, int nlines, uint8_t **packed_off,
                                 size_t *packed_off_size,
                                 uint64_t *unpacked_off_size,
                                 uint8_t **packed_samp, size_t *packed_samp_size,
                                 uint64_t *unpacked_samp_size) {
    const exr_header *h = &part->header;
    int width = part->width, ymin = h->data_window.min_y, nch = h->num_channels;
    size_t sample_size = 0, notab, usamp;
    int32_t *otab = NULL;
    uint8_t *sd = NULL, *sp;
    uint64_t cum = 0;
    int r2, x, c;
    exr_result rc = EXR_SUCCESS;

    *packed_off = *packed_samp = NULL;
    for (c = 0; c < nch; ++c)
        sample_size += exr_pixel_size(h->channels[c].pixel_type);

    /* offset table (cumulative within the block, row-major) */
    notab = (size_t)width * nlines;
    otab = (int32_t *)exr_malloc(a, notab * sizeof(int32_t));
    if (!otab) return EXR_ERROR_OUT_OF_MEMORY;
    for (r2 = 0; r2 < nlines; ++r2) {
        int row = y0 - ymin + r2;
        for (x = 0; x < width; ++x) {
            cum += (uint64_t)part->deep_sample_counts[(size_t)row * width + x];
            otab[r2 * width + x] = (int32_t)cum;
        }
    }
    *unpacked_off_size = notab * sizeof(int32_t);
    rc = deep_compress(a, comp, (uint8_t *)otab, notab * sizeof(int32_t),
                       packed_off, packed_off_size);
    if (!EXR_OK(rc)) goto done;

    /* sample data (channel-planar over the block's pixels) */
    usamp = (size_t)cum * sample_size;
    sd = (uint8_t *)exr_malloc(a, usamp ? usamp : 1);
    if (!sd) { rc = EXR_ERROR_OUT_OF_MEMORY; goto done; }
    sp = sd;
    for (c = 0; c < nch; ++c) {
        size_t ps = exr_pixel_size(h->channels[c].pixel_type);
        for (r2 = 0; r2 < nlines; ++r2) {
            int row = y0 - ymin + r2;
            for (x = 0; x < width; ++x) {
                size_t pix = (size_t)row * width + x;
                int cnt = part->deep_sample_counts[pix];
                memcpy(sp, (const uint8_t *)part->deep_images[c] + prefix[pix] * ps,
                       (size_t)cnt * ps);
                sp += (size_t)cnt * ps;
            }
        }
    }
    *unpacked_samp_size = usamp;
    rc = deep_compress(a, comp, sd, usamp, packed_samp, packed_samp_size);

done:
    exr_free(a, otab);
    exr_free(a, sd);
    if (!EXR_OK(rc)) {
        exr_free(a, *packed_off);
        *packed_off = NULL;
    }
    return rc;
}

/* ---- deep tiled read (ONE_LEVEL / level 0) -------------------------------- */

exr_result exr_read_deep_tiled_part(exr_reader *r, exr_int_part *p,
                                    int32_t part_idx, exr_part *out) {
    const exr_allocator *a = &r->alloc;
    const exr_header *h = &p->header;
    int width = p->width, height = p->height, nch = h->num_channels;
    int tx = (int)h->tile_x_size, ty = (int)h->tile_y_size;
    int nxt, nyt, txi, tyi, c;
    size_t sample_size = 0, npix;
    uint64_t total = 0, *prefix = NULL, *run = NULL;
    int32_t *otab = NULL;
    uint8_t *sbuf = NULL;
    size_t otab_cap = 0, sbuf_cap = 0;
    exr_result rc = EXR_SUCCESS;

    if (tx <= 0 || ty <= 0 || width <= 0 || height <= 0) return EXR_ERROR_CORRUPT;
    nxt = (width + tx - 1) / tx;
    nyt = (height + ty - 1) / ty;
    for (c = 0; c < nch; ++c)
        sample_size += exr_pixel_size(h->channels[c].pixel_type);
    if (sample_size == 0) return EXR_ERROR_CORRUPT;
    {
        size_t prefix_bytes;
        if (exr_mul_ovf((size_t)width, (size_t)height, &npix) ||
            exr_mul_ovf(npix, sizeof(uint64_t), &prefix_bytes))
            return EXR_ERROR_CORRUPT;
        prefix = (uint64_t *)exr_malloc(a, prefix_bytes ? prefix_bytes : 1);
    }

    out->is_deep = 1;
    out->deep_sample_counts = (int32_t *)exr_calloc(a, npix, sizeof(int32_t));
    out->deep_images = (void **)exr_calloc(a, nch ? (size_t)nch : 1, sizeof(void *));
    run = (uint64_t *)exr_calloc(a, nch ? (size_t)nch : 1, sizeof(uint64_t));
    if (!out->deep_sample_counts || !out->deep_images || !prefix || !run) {
        rc = EXR_ERROR_OUT_OF_MEMORY;
        goto done;
    }

    /* Pass 1: tile offset tables -> per-pixel counts. */
    for (tyi = 0; tyi < nyt; ++tyi) {
        for (txi = 0; txi < nxt; ++txi) {
            uint32_t idx = (uint32_t)tyi * nxt + txi;
            uint64_t off, pots, psds, usds, prev = 0;
            const uint8_t *hdr, *packed;
            size_t hsz = r->is_multipart ? 44 : 40;
            int tw = (tx < width - txi * tx) ? tx : (width - txi * tx);
            int th = (ty < height - tyi * ty) ? ty : (height - tyi * ty);
            size_t need;
            int rr, x;

            if (idx >= p->num_chunks) { rc = EXR_ERROR_CORRUPT; goto done; }
            off = p->offsets[idx];
            rc = exr_reader_fetch(r, off, hsz, NULL, &hdr);
            if (!EXR_OK(rc)) goto done;
            if (r->is_multipart) {
                if (exr_rd_i32(hdr) != part_idx) { rc = EXR_ERROR_CORRUPT; goto done; }
                hdr += 4;
            }
            if (exr_rd_i32(hdr) != txi || exr_rd_i32(hdr + 4) != tyi ||
                exr_rd_i32(hdr + 8) != 0 || exr_rd_i32(hdr + 12) != 0) {
                rc = EXR_ERROR_CORRUPT;
                goto done;
            }
            pots = exr_rd_u64(hdr + 16);
            psds = exr_rd_u64(hdr + 24);
            usds = exr_rd_u64(hdr + 32);
            (void)psds; (void)usds;
            if (exr_mul_ovf((size_t)tw, (size_t)th, &need)) {
                rc = EXR_ERROR_CORRUPT; goto done;
            }
            if (need > otab_cap) {
                size_t nb;
                if (exr_mul_ovf(need, sizeof(int32_t), &nb)) {
                    rc = EXR_ERROR_CORRUPT; goto done;
                }
                exr_free(a, otab);
                otab = (int32_t *)exr_malloc(a, nb);
                if (!otab) { rc = EXR_ERROR_OUT_OF_MEMORY; goto done; }
                otab_cap = need;
            }
            rc = exr_reader_fetch(r, off + hsz, (size_t)pots, NULL, &packed);
            if (!EXR_OK(rc)) goto done;
            rc = deep_decompress(a, h->compression, packed, (size_t)pots,
                                 (uint8_t *)otab, need * sizeof(int32_t));
            if (!EXR_OK(rc)) goto done;
            for (rr = 0; rr < th; ++rr) {
                for (x = 0; x < tw; ++x) {
                    int32_t cum = otab[rr * tw + x];
                    int64_t cnt = (int64_t)cum - (int64_t)prev;
                    size_t pix = (size_t)(tyi * ty + rr) * width + (txi * tx + x);
                    /* The offset table is attacker-controlled (stored verbatim
                     * for NONE); a non-monotonic table would yield negative
                     * counts -> wrapped prefix sums and heap OOB in pass 2. */
                    if (cnt < 0) { rc = EXR_ERROR_CORRUPT; goto done; }
                    out->deep_sample_counts[pix] = (int32_t)cnt;
                    prev = (uint64_t)cum;
                }
            }
            total += prev;
        }
    }

    { size_t i; uint64_t acc = 0;
      for (i = 0; i < npix; ++i) { prefix[i] = acc; acc += (uint64_t)out->deep_sample_counts[i]; } }
    out->deep_total_samples = total;
    for (c = 0; c < nch; ++c) {
        size_t bytes;
        if (exr_mul_ovf((size_t)total, exr_pixel_size(h->channels[c].pixel_type), &bytes)) {
            rc = EXR_ERROR_CORRUPT; goto done;
        }
        out->deep_images[c] = exr_calloc(a, bytes ? bytes : 1, 1);
        if (!out->deep_images[c]) { rc = EXR_ERROR_OUT_OF_MEMORY; goto done; }
    }
    (void)run;

    /* Pass 2: tile sample data -> scatter per pixel. */
    for (tyi = 0; tyi < nyt; ++tyi) {
        for (txi = 0; txi < nxt; ++txi) {
            uint32_t idx = (uint32_t)tyi * nxt + txi;
            uint64_t off, pots, psds, usds, tile_total = 0;
            const uint8_t *hdr, *packed;
            size_t hsz = r->is_multipart ? 44 : 40, base = 0;
            int tw = (tx < width - txi * tx) ? tx : (width - txi * tx);
            int th = (ty < height - tyi * ty) ? ty : (height - tyi * ty);
            int rr, x;

            off = p->offsets[idx];
            rc = exr_reader_fetch(r, off, hsz, NULL, &hdr);
            if (!EXR_OK(rc)) goto done;
            if (r->is_multipart) hdr += 4;
            pots = exr_rd_u64(hdr + 16);
            psds = exr_rd_u64(hdr + 24);
            usds = exr_rd_u64(hdr + 32);
            for (rr = 0; rr < th; ++rr)
                for (x = 0; x < tw; ++x)
                    tile_total += (uint64_t)out->deep_sample_counts[(size_t)(tyi * ty + rr) * width + (txi * tx + x)];
            if (usds != tile_total * sample_size) { rc = EXR_ERROR_CORRUPT; goto done; }
            if ((size_t)usds > sbuf_cap) {
                exr_free(a, sbuf);
                sbuf = (uint8_t *)exr_malloc(a, (size_t)usds ? (size_t)usds : 1);
                if (!sbuf) { rc = EXR_ERROR_OUT_OF_MEMORY; goto done; }
                sbuf_cap = (size_t)usds;
            }
            rc = exr_reader_fetch(r, off + hsz + pots, (size_t)psds, NULL, &packed);
            if (!EXR_OK(rc)) goto done;
            rc = deep_decompress(a, h->compression, packed, (size_t)psds, sbuf, (size_t)usds);
            if (!EXR_OK(rc)) goto done;
            for (c = 0; c < nch; ++c) {
                size_t ps = exr_pixel_size(h->channels[c].pixel_type), local = 0;
                for (rr = 0; rr < th; ++rr) {
                    for (x = 0; x < tw; ++x) {
                        size_t pix = (size_t)(tyi * ty + rr) * width + (txi * tx + x);
                        int cnt = out->deep_sample_counts[pix];
                        memcpy((uint8_t *)out->deep_images[c] + prefix[pix] * ps,
                               sbuf + base + local * ps, (size_t)cnt * ps);
                        local += (size_t)cnt;
                    }
                }
                base += tile_total * ps;
            }
        }
    }

done:
    exr_free(a, otab);
    exr_free(a, sbuf);
    exr_free(a, prefix);
    exr_free(a, run);
    return rc;
}

exr_result exr_deep_encode_tile(const exr_allocator *a, exr_compression comp,
                                const exr_part *part, const uint64_t *prefix,
                                int x0, int y0, int tile_w, int tile_h,
                                uint8_t **packed_off, size_t *packed_off_size,
                                uint64_t *unpacked_off_size, uint8_t **packed_samp,
                                size_t *packed_samp_size,
                                uint64_t *unpacked_samp_size) {
    const exr_header *h = &part->header;
    int width = part->width, nch = h->num_channels;
    size_t sample_size = 0, notab, usamp;
    int32_t *otab = NULL;
    uint8_t *sd = NULL, *sp;
    uint64_t cum = 0;
    int rr, x, c;
    exr_result rc = EXR_SUCCESS;

    *packed_off = *packed_samp = NULL;
    for (c = 0; c < nch; ++c)
        sample_size += exr_pixel_size(h->channels[c].pixel_type);

    notab = (size_t)tile_w * tile_h;
    otab = (int32_t *)exr_malloc(a, notab * sizeof(int32_t));
    if (!otab) return EXR_ERROR_OUT_OF_MEMORY;
    for (rr = 0; rr < tile_h; ++rr)
        for (x = 0; x < tile_w; ++x) {
            cum += (uint64_t)part->deep_sample_counts[(size_t)(y0 + rr) * width + (x0 + x)];
            otab[rr * tile_w + x] = (int32_t)cum;
        }
    *unpacked_off_size = notab * sizeof(int32_t);
    rc = deep_compress(a, comp, (uint8_t *)otab, notab * sizeof(int32_t),
                       packed_off, packed_off_size);
    if (!EXR_OK(rc)) goto done;

    usamp = (size_t)cum * sample_size;
    sd = (uint8_t *)exr_malloc(a, usamp ? usamp : 1);
    if (!sd) { rc = EXR_ERROR_OUT_OF_MEMORY; goto done; }
    sp = sd;
    for (c = 0; c < nch; ++c) {
        size_t ps = exr_pixel_size(h->channels[c].pixel_type);
        for (rr = 0; rr < tile_h; ++rr)
            for (x = 0; x < tile_w; ++x) {
                size_t pix = (size_t)(y0 + rr) * width + (x0 + x);
                int cnt = part->deep_sample_counts[pix];
                memcpy(sp, (const uint8_t *)part->deep_images[c] + prefix[pix] * ps,
                       (size_t)cnt * ps);
                sp += (size_t)cnt * ps;
            }
    }
    *unpacked_samp_size = usamp;
    rc = deep_compress(a, comp, sd, usamp, packed_samp, packed_samp_size);

done:
    exr_free(a, otab);
    exr_free(a, sd);
    if (!EXR_OK(rc)) { exr_free(a, *packed_off); *packed_off = NULL; }
    return rc;
}

/* ---- deep mip/ripmap level generation (point subsample) ------------------- */

exr_result exr_deep_build_level(const exr_allocator *a, const exr_part *src,
                                const uint64_t *src_prefix, int lw, int lh,
                                exr_part *out) {
    int W = src->width, H = src->height, nch = src->header.num_channels, c, x, y;
    size_t npix;
    uint64_t total = 0;
    exr_result rc = EXR_SUCCESS;

    memset(out, 0, sizeof(*out));
    if (lw <= 0 || lh <= 0 || exr_mul_ovf((size_t)lw, (size_t)lh, &npix))
        return EXR_ERROR_CORRUPT;
    out->header = src->header; /* shallow: channels shared, not owned */
    out->width = lw;
    out->height = lh;
    out->is_deep = 1;
    out->deep_sample_counts = (int32_t *)exr_calloc(a, npix, sizeof(int32_t));
    out->deep_images = (void **)exr_calloc(a, nch ? (size_t)nch : 1, sizeof(void *));
    if (!out->deep_sample_counts || !out->deep_images) {
        rc = EXR_ERROR_OUT_OF_MEMORY;
        goto fail;
    }
    for (y = 0; y < lh; ++y) {
        int sy = (int)((int64_t)y * H / lh);
        if (sy >= H) sy = H - 1;
        for (x = 0; x < lw; ++x) {
            int sx = (int)((int64_t)x * W / lw), cnt;
            if (sx >= W) sx = W - 1;
            cnt = src->deep_sample_counts[(size_t)sy * W + sx];
            out->deep_sample_counts[(size_t)y * lw + x] = cnt;
            total += (uint64_t)cnt;
        }
    }
    out->deep_total_samples = total;
    for (c = 0; c < nch; ++c) {
        size_t ps = exr_pixel_size(src->header.channels[c].pixel_type);
        size_t off = 0, bytes;
        uint8_t *dst;
        if (exr_mul_ovf((size_t)total, ps, &bytes)) { rc = EXR_ERROR_CORRUPT; goto fail; }
        out->deep_images[c] = exr_malloc(a, bytes ? bytes : 1);
        if (!out->deep_images[c]) { rc = EXR_ERROR_OUT_OF_MEMORY; goto fail; }
        dst = (uint8_t *)out->deep_images[c];
        for (y = 0; y < lh; ++y) {
            int sy = (int)((int64_t)y * H / lh);
            if (sy >= H) sy = H - 1;
            for (x = 0; x < lw; ++x) {
                int sx = (int)((int64_t)x * W / lw), cnt;
                if (sx >= W) sx = W - 1;
                cnt = out->deep_sample_counts[(size_t)y * lw + x];
                memcpy(dst + off * ps,
                       (const uint8_t *)src->deep_images[c] +
                           src_prefix[(size_t)sy * W + sx] * ps,
                       (size_t)cnt * ps);
                off += (size_t)cnt;
            }
        }
    }
    return EXR_SUCCESS;

fail:
    exr_deep_level_free(a, out);
    return rc;
}

void exr_deep_level_free(const exr_allocator *a, exr_part *level) {
    int c;
    if (!level) return;
    if (level->deep_images) {
        for (c = 0; c < level->header.num_channels; ++c)
            exr_free(a, level->deep_images[c]);
        exr_free(a, level->deep_images);
    }
    exr_free(a, level->deep_sample_counts);
    /* header is shared with the source — do not free it */
    level->deep_images = NULL;
    level->deep_sample_counts = NULL;
}
