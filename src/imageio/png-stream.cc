// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-present Light Transport Entertainment, Inc.
#include "imageio/png-stream.hh"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "external/miniz.h"
// Declarations only — the implementation lives in image-util.cc.
#include "external/stb_image_resize2.h"

namespace tinyusdz {
namespace imageio {

namespace {

inline uint32_t rd32be(const uint8_t *p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) |
         uint32_t(p[3]);
}

inline uint8_t paeth(int a, int b, int c) {
  int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b),
      pc = std::abs(p - c);
  if (pa <= pb && pa <= pc) return (uint8_t)a;
  if (pb <= pc) return (uint8_t)b;
  return (uint8_t)c;
}

bool DeriveLayout(const PngImageInfo &in, PngImageInfo *out) {
  uint8_t ct = in.color_type, bd = in.bit_depth;
  uint8_t channels;
  if (ct == 0) channels = 1;
  else if (ct == 2) channels = 3;
  else if (ct == 3) channels = 1;
  else if (ct == 4) channels = 2;
  else if (ct == 6) channels = 4;
  else return false;
  // Palette indices are <= 8-bit; grayscale also allows sub-byte; all non-palette
  // types allow 16-bit. (PNG filtering is byte-wise with bytes_per_pixel below,
  // so the codec is bit-depth-agnostic for lossless transcode. ResizePNG /
  // ConvertColorspacePNG keep their own 8-bit guards — they interpret samples,
  // which would need endian handling for 16-bit.)
  if (ct == 3) {
    if (!(bd == 1 || bd == 2 || bd == 4 || bd == 8)) return false;
  } else if (ct == 0) {
    if (!(bd == 1 || bd == 2 || bd == 4 || bd == 8 || bd == 16)) return false;
  } else {  // RGB / gray+alpha / RGBA
    if (!(bd == 8 || bd == 16)) return false;
  }
  const size_t bits_per_pixel = (size_t)bd * channels;
  *out = in;
  out->channels = channels;
  out->bytes_per_pixel = (uint32_t)((bits_per_pixel + 7) / 8);  // >= 1
  out->row_bytes = (uint32_t)(((size_t)in.width * bits_per_pixel + 7) / 8);
  return true;
}

}  // namespace

// ----------------------------------------------------------------------------
// PngScanlineReader
// ----------------------------------------------------------------------------

struct PngScanlineReader::Impl {
  mz_stream istrm;
  bool istrm_init = false;
  std::vector<std::pair<const uint8_t *, uint32_t>> idats;  // (data, len)
  size_t idat_idx = 0;
  std::vector<uint8_t> scan;        // one filtered scanline (stride bytes)
  std::vector<uint8_t> prev, cur;   // raw rows (row_bytes)
  size_t scan_filled = 0;
  size_t bpp = 1, rowlen = 0, stride = 0;
  bool stream_end = false;

  ~Impl() {
    if (istrm_init) mz_inflateEnd(&istrm);
  }
};

PngScanlineReader::PngScanlineReader() : impl_(new Impl()) {}
PngScanlineReader::~PngScanlineReader() { delete impl_; }

bool PngScanlineReader::Open(const uint8_t *data, size_t size) {
  static const uint8_t SIG[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (size < 8 || std::memcmp(data, SIG, 8) != 0) return false;

  PngImageInfo raw{};
  bool have_ihdr = false;
  for (size_t pos = 8; pos + 12 <= size;) {
    uint32_t len = rd32be(data + pos);
    const uint8_t *type = data + pos + 4;
    if (pos + 12 + (size_t)len > size) return false;  // truncated
    const uint8_t *cdata = data + pos + 8;
    size_t total = 12 + (size_t)len;
    if (std::memcmp(type, "IHDR", 4) == 0) {
      if (len < 13) return false;
      raw.width = rd32be(cdata);
      raw.height = rd32be(cdata + 4);
      raw.bit_depth = cdata[8];
      raw.color_type = cdata[9];
      if (cdata[10] != 0 || cdata[11] != 0 || cdata[12] != 0)
        return false;  // compression/filter/interlace must be 0/0/0
      have_ihdr = true;
    } else if (std::memcmp(type, "IDAT", 4) == 0) {
      impl_->idats.emplace_back(cdata, len);
    }
    chunks_.emplace_back(data + pos, total);
    pos += total;
    if (std::memcmp(type, "IEND", 4) == 0) break;
  }
  if (!have_ihdr || impl_->idats.empty() || raw.width == 0 || raw.height == 0)
    return false;
  if (!DeriveLayout(raw, &info_)) return false;

  impl_->bpp = info_.bytes_per_pixel;
  impl_->rowlen = info_.row_bytes;
  impl_->stride = info_.row_bytes + 1;
  impl_->scan.resize(impl_->stride);
  impl_->prev.assign(impl_->rowlen, 0);
  impl_->cur.resize(impl_->rowlen);

  std::memset(&impl_->istrm, 0, sizeof(impl_->istrm));
  if (mz_inflateInit(&impl_->istrm) != MZ_OK) return false;
  impl_->istrm_init = true;

  ok_ = true;
  return true;
}

bool PngScanlineReader::NextRow(uint8_t *row) {
  if (!ok_ || rows_done_ >= info_.height) return false;
  Impl &m = *impl_;
  mz_stream &is = m.istrm;

  // Inflate until one full filtered scanline (stride bytes) is buffered, driving
  // the loop by output progress (miniz buffers compressed input internally, so a
  // single IDAT can yield many scanlines across calls). avail_out!=0 with
  // avail_in>0 is just miniz's 32KB dictionary boundary, not end of input.
  while (m.scan_filled < m.stride) {
    if (is.avail_in == 0 && !m.stream_end && m.idat_idx < m.idats.size()) {
      is.next_in = m.idats[m.idat_idx].first;
      is.avail_in = m.idats[m.idat_idx].second;
      ++m.idat_idx;
    }
    is.next_out = m.scan.data() + m.scan_filled;
    is.avail_out = (unsigned)(m.stride - m.scan_filled);
    int r = mz_inflate(&is, MZ_NO_FLUSH);
    m.scan_filled = m.stride - is.avail_out;
    if (r == MZ_STREAM_END) {
      m.stream_end = true;
      break;
    }
    if (r == MZ_OK) {
      continue;  // loop re-checks scan_filled / advances to next IDAT
    }
    if (r == MZ_BUF_ERROR) {
      if (is.avail_in == 0 && m.idat_idx >= m.idats.size()) break;  // no more input
      continue;  // needs the next IDAT chunk
    }
    ok_ = false;
    return false;  // hard inflate error
  }
  if (m.scan_filled < m.stride) {
    ok_ = false;
    return false;  // truncated stream
  }
  m.scan_filled = 0;

  // Unfilter scan -> cur using prev.
  const uint8_t ft = m.scan[0];
  const uint8_t *f = m.scan.data() + 1;
  const size_t bpp = m.bpp, rowlen = m.rowlen;
  for (size_t i = 0; i < rowlen; ++i) {
    uint8_t a = (i >= bpp) ? m.cur[i - bpp] : 0;
    uint8_t b = m.prev[i];
    uint8_t c = (i >= bpp) ? m.prev[i - bpp] : 0;
    uint8_t v;
    switch (ft) {
      case 0: v = f[i]; break;
      case 1: v = (uint8_t)(f[i] + a); break;
      case 2: v = (uint8_t)(f[i] + b); break;
      case 3: v = (uint8_t)(f[i] + (uint8_t)((a + b) >> 1)); break;
      case 4: v = (uint8_t)(f[i] + paeth(a, b, c)); break;
      default:
        ok_ = false;
        return false;  // invalid filter type
    }
    m.cur[i] = v;
  }
  std::memcpy(row, m.cur.data(), rowlen);
  m.prev.swap(m.cur);
  ++rows_done_;
  return true;
}

// ----------------------------------------------------------------------------
// PngScanlineWriter
// ----------------------------------------------------------------------------

struct PngScanlineWriter::Impl {
  mz_stream dstrm;
  bool dstrm_init = false;
  std::vector<uint8_t> idat_out;
  std::vector<uint8_t> dbuf;       // 64KB deflate output staging
  std::vector<uint8_t> prev, filt; // prev raw row, filtered scanline (stride)
  bool first_row = true;
  size_t bpp = 1, rowlen = 0, stride = 0;
  // Output metadata.
  bool passthrough = false;
  const PngScanlineReader *templ = nullptr;  // passthrough: copy its chunks
  PngImageInfo info{};                        // fresh: synthesize IHDR

  ~Impl() {
    if (dstrm_init) mz_deflateEnd(&dstrm);
  }

  bool init_common(const PngImageInfo &i, int level) {
    bpp = i.bytes_per_pixel;
    rowlen = i.row_bytes;
    stride = i.row_bytes + 1;
    prev.assign(rowlen, 0);
    filt.resize(stride);
    dbuf.resize(1 << 16);
    idat_out.reserve(rowlen * 4 + 4096);
    std::memset(&dstrm, 0, sizeof(dstrm));
    if (mz_deflateInit2(&dstrm, level, MZ_DEFLATED, MZ_DEFAULT_WINDOW_BITS, 9,
                        MZ_DEFAULT_STRATEGY) != MZ_OK)
      return false;
    dstrm_init = true;
    return true;
  }

  bool pump(const uint8_t *p, size_t n, int flush) {
    dstrm.next_in = p;
    dstrm.avail_in = (unsigned)n;
    for (;;) {
      dstrm.next_out = dbuf.data();
      dstrm.avail_out = (unsigned)dbuf.size();
      int r = mz_deflate(&dstrm, flush);
      if (r != MZ_OK && r != MZ_STREAM_END && r != MZ_BUF_ERROR) return false;
      size_t produced = dbuf.size() - dstrm.avail_out;
      if (produced)
        idat_out.insert(idat_out.end(), dbuf.data(), dbuf.data() + produced);
      if (r == MZ_STREAM_END) break;
      if (dstrm.avail_out != 0) {
        if (flush != MZ_FINISH) break;
      }
    }
    return true;
  }
};

PngScanlineWriter::PngScanlineWriter() : impl_(new Impl()) {}
PngScanlineWriter::~PngScanlineWriter() { delete impl_; }

bool PngScanlineWriter::BeginPassthrough(const PngScanlineReader &templ,
                                         int deflate_level) {
  impl_->passthrough = true;
  impl_->templ = &templ;
  impl_->info = templ.info();
  ok_ = impl_->init_common(templ.info(), deflate_level);
  return ok_;
}

bool PngScanlineWriter::Begin(const PngImageInfo &info, int deflate_level) {
  PngImageInfo derived;
  if (!DeriveLayout(info, &derived)) return false;
  impl_->passthrough = false;
  impl_->info = derived;
  ok_ = impl_->init_common(derived, deflate_level);
  return ok_;
}

bool PngScanlineWriter::WriteRow(const uint8_t *row) {
  if (!ok_) return false;
  Impl &m = *impl_;
  const size_t bpp = m.bpp, rowlen = m.rowlen;
  const uint8_t *cur = row;
  const uint8_t *prev = m.prev.data();

  // Pick the filter minimizing the sum of |signed bytes| (libpng heuristic).
  int best = 0;
  uint64_t best_sum = UINT64_MAX;
  for (int t = 0; t < 5; ++t) {
    uint64_t sum = 0;
    for (size_t i = 0; i < rowlen; ++i) {
      uint8_t a = (i >= bpp) ? cur[i - bpp] : 0, b = prev[i],
              c = (i >= bpp) ? prev[i - bpp] : 0, fv;
      switch (t) {
        case 0: fv = cur[i]; break;
        case 1: fv = (uint8_t)(cur[i] - a); break;
        case 2: fv = (uint8_t)(cur[i] - b); break;
        case 3: fv = (uint8_t)(cur[i] - (uint8_t)((a + b) >> 1)); break;
        default: fv = (uint8_t)(cur[i] - paeth(a, b, c)); break;
      }
      int8_t s = (int8_t)fv;
      sum += (uint64_t)(s < 0 ? -s : s);
    }
    if (sum < best_sum) {
      best_sum = sum;
      best = t;
    }
  }
  m.filt[0] = (uint8_t)best;
  for (size_t i = 0; i < rowlen; ++i) {
    uint8_t a = (i >= bpp) ? cur[i - bpp] : 0, b = prev[i],
            c = (i >= bpp) ? prev[i - bpp] : 0, fv;
    switch (best) {
      case 0: fv = cur[i]; break;
      case 1: fv = (uint8_t)(cur[i] - a); break;
      case 2: fv = (uint8_t)(cur[i] - b); break;
      case 3: fv = (uint8_t)(cur[i] - (uint8_t)((a + b) >> 1)); break;
      default: fv = (uint8_t)(cur[i] - paeth(a, b, c)); break;
    }
    m.filt[1 + i] = fv;
  }
  if (!m.pump(m.filt.data(), m.stride, MZ_NO_FLUSH)) {
    ok_ = false;
    return false;
  }
  std::memcpy(m.prev.data(), row, rowlen);
  return true;
}

bool PngScanlineWriter::Finish(std::vector<uint8_t> &out) {
  if (!ok_) return false;
  Impl &m = *impl_;
  if (!m.pump(nullptr, 0, MZ_FINISH)) {
    ok_ = false;
    return false;
  }

  static const uint8_t SIG[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  out.clear();
  out.reserve(m.idat_out.size() + 1024);
  out.insert(out.end(), SIG, SIG + 8);
  auto wr32 = [&](uint32_t v) {
    out.push_back((uint8_t)(v >> 24));
    out.push_back((uint8_t)(v >> 16));
    out.push_back((uint8_t)(v >> 8));
    out.push_back((uint8_t)v);
  };
  auto write_chunk = [&](const char type[4], const uint8_t *p, size_t n) {
    wr32((uint32_t)n);
    size_t tpos = out.size();
    out.insert(out.end(), type, type + 4);
    if (n) out.insert(out.end(), p, p + n);
    mz_ulong c = mz_crc32(MZ_CRC32_INIT, out.data() + tpos, 4 + n);
    wr32((uint32_t)c);
  };

  if (m.passthrough && m.templ) {
    // Copy original chunks verbatim, replacing the IDAT run with the new IDAT.
    bool idat_emitted = false;
    for (auto &ck : m.templ->chunks()) {
      const uint8_t *st = ck.first;
      if (std::memcmp(st + 4, "IDAT", 4) == 0) {
        if (idat_emitted) continue;
        idat_emitted = true;
        write_chunk("IDAT", m.idat_out.data(), m.idat_out.size());
      } else {
        out.insert(out.end(), st, st + ck.second);
      }
    }
  } else {
    // Fresh: signature + IHDR + IDAT + IEND (no palette/ancillary).
    uint8_t ihdr[13];
    ihdr[0] = (uint8_t)(m.info.width >> 24);
    ihdr[1] = (uint8_t)(m.info.width >> 16);
    ihdr[2] = (uint8_t)(m.info.width >> 8);
    ihdr[3] = (uint8_t)m.info.width;
    ihdr[4] = (uint8_t)(m.info.height >> 24);
    ihdr[5] = (uint8_t)(m.info.height >> 16);
    ihdr[6] = (uint8_t)(m.info.height >> 8);
    ihdr[7] = (uint8_t)m.info.height;
    ihdr[8] = m.info.bit_depth;
    ihdr[9] = m.info.color_type;
    ihdr[10] = 0;  // compression
    ihdr[11] = 0;  // filter
    ihdr[12] = 0;  // interlace
    write_chunk("IHDR", ihdr, 13);
    write_chunk("IDAT", m.idat_out.data(), m.idat_out.size());
    write_chunk("IEND", nullptr, 0);
  }
  return true;
}

// ----------------------------------------------------------------------------
// TranscodePNG (streaming PNG -> PNG re-encode)
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// ResizePNG (streaming PNG -> resized PNG via stb_image_resize2 callbacks)
// ----------------------------------------------------------------------------

namespace {

struct ResizeCtx {
  PngScanlineReader *reader = nullptr;
  PngScanlineWriter *writer = nullptr;
  uint32_t in_w = 0;
  int rs_channels = 0;       // sample channels fed to the resampler
  bool palette = false;
  const uint8_t *plte = nullptr;  // 256*3 RGB
  size_t plte_entries = 0;
  const uint8_t *trns = nullptr;  // palette alpha
  size_t trns_len = 0;
  int bps = 1;               // bytes per sample (1 for 8-bit, 2 for 16-bit)
  bool swap16 = false;       // 16-bit on a little-endian host -> byte-swap pairs
  std::vector<uint8_t> raw_row;     // reader's native scanline
  std::vector<uint8_t> sample_row;  // palette-expanded or endian-swapped samples
  std::vector<uint8_t> out_swap;    // 16-bit output: native -> big-endian buffer
  long cur_y = -1;                  // last decoded input row index
  uint32_t out_rows = 0;
  bool error = false;
};

// Swap byte pairs (PNG 16-bit is big-endian; stbir UINT16 wants host-native).
inline void SwapPairs(const uint8_t *in, uint8_t *out, size_t nsamples) {
  for (size_t i = 0; i < nsamples; ++i) {
    out[i * 2 + 0] = in[i * 2 + 1];
    out[i * 2 + 1] = in[i * 2 + 0];
  }
}

const void *ResizeInputCb(void *opt_out, const void *plane, int num_pixels,
                          int x, int y, void *ud) {
  (void)opt_out;
  (void)plane;
  (void)num_pixels;
  ResizeCtx *c = static_cast<ResizeCtx *>(ud);
  while (c->cur_y < y) {
    if (!c->reader->NextRow(c->raw_row.data())) {
      c->error = true;
      break;
    }
    ++c->cur_y;
    if (c->palette) {
      uint8_t *o = c->sample_row.data();
      const uint8_t *idxs = c->raw_row.data();
      const int ch = c->rs_channels;
      for (uint32_t i = 0; i < c->in_w; ++i) {
        uint32_t idx = idxs[i];
        const uint8_t *p =
            (idx < c->plte_entries) ? (c->plte + idx * 3) : c->plte;
        o[i * ch + 0] = p[0];
        o[i * ch + 1] = p[1];
        o[i * ch + 2] = p[2];
        if (ch == 4)
          o[i * ch + 3] = (c->trns && idx < c->trns_len) ? c->trns[idx] : 255;
      }
    } else if (c->swap16) {
      SwapPairs(c->raw_row.data(), c->sample_row.data(),
                (size_t)c->in_w * c->rs_channels);
    }
  }
  const uint8_t *base = (c->palette || c->swap16) ? c->sample_row.data()
                                                  : c->raw_row.data();
  return base + (size_t)x * c->rs_channels * c->bps;
}

void ResizeOutputCb(const void *out_ptr, int num_pixels, int y, void *ud) {
  (void)y;
  ResizeCtx *c = static_cast<ResizeCtx *>(ud);
  const uint8_t *row = static_cast<const uint8_t *>(out_ptr);
  if (c->swap16) {
    SwapPairs(row, c->out_swap.data(), (size_t)num_pixels * c->rs_channels);
    row = c->out_swap.data();
  }
  if (!c->writer->WriteRow(row)) {
    c->error = true;
  } else {
    ++c->out_rows;
  }
}

}  // namespace

bool ConvertColorspacePNG(const uint8_t *data, size_t size, ColorspaceXform xf,
                          std::vector<uint8_t> &out) {
  PngScanlineReader reader;
  if (!reader.Open(data, size)) return false;
  const PngImageInfo &in = reader.info();
  if (in.bit_depth != 8 || in.color_type == 3) return false;  // palette/sub-byte

  // Build the 8-bit sRGB<->linear LUT.
  uint8_t lut[256];
  for (int i = 0; i < 256; ++i) {
    float c = float(i) / 255.0f, o;
    if (xf == ColorspaceXform::SrgbToLinear) {
      o = (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
    } else {
      o = (c <= 0.0031308f) ? (c * 12.92f)
                            : (1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
    }
    int v = int(o * 255.0f + 0.5f);
    lut[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
  }
  // Color channels to transform (alpha is preserved): 1ch gray -> ch0;
  // 2ch gray+alpha -> ch0; 3ch RGB / 4ch RGBA -> ch0..2.
  const int total_ch = in.channels;
  const int ncolor = (total_ch <= 2) ? 1 : 3;

  PngScanlineWriter writer;
  if (!writer.Begin(in)) return false;  // fresh: drop stale color-profile chunks
  std::vector<uint8_t> row(in.row_bytes);
  for (uint32_t y = 0; y < in.height; ++y) {
    if (!reader.NextRow(row.data())) return false;
    uint8_t *r = row.data();
    for (uint32_t x = 0; x < in.width; ++x) {
      uint8_t *px = r + (size_t)x * total_ch;
      for (int c = 0; c < ncolor; ++c) px[c] = lut[px[c]];
    }
    if (!writer.WriteRow(row.data())) return false;
  }
  if (!reader.ok()) return false;
  return writer.Finish(out);
}

bool ResizePNG(const uint8_t *data, size_t size, uint32_t out_w, uint32_t out_h,
               bool srgb, std::vector<uint8_t> &out) {
  if (out_w == 0 || out_h == 0) return false;
  PngScanlineReader reader;
  if (!reader.Open(data, size)) return false;
  const PngImageInfo &in = reader.info();
  if (in.bit_depth != 8 && in.bit_depth != 16) return false;  // sub-byte -> fall back
  if (in.color_type == 3 && in.bit_depth != 8) return false;   // palette is 8-bit

  ResizeCtx ctx;
  ctx.reader = &reader;
  ctx.in_w = in.width;
  ctx.bps = in.bit_depth / 8;  // 1 or 2
  {
    const uint16_t one = 1;
    ctx.swap16 = (ctx.bps == 2) && (*reinterpret_cast<const uint8_t *>(&one) == 1);
  }

  uint8_t out_ct = in.color_type;
  uint8_t out_bd = (uint8_t)in.bit_depth;
  if (in.color_type == 3) {  // palette (8-bit) -> RGB / RGBA 8-bit
    ctx.palette = true;
    for (auto &ck : reader.chunks()) {
      const uint8_t *st = ck.first;
      uint32_t len = (uint32_t(st[0]) << 24) | (uint32_t(st[1]) << 16) |
                     (uint32_t(st[2]) << 8) | uint32_t(st[3]);
      if (std::memcmp(st + 4, "PLTE", 4) == 0) {
        ctx.plte = st + 8;
        ctx.plte_entries = len / 3;
      } else if (std::memcmp(st + 4, "tRNS", 4) == 0) {
        ctx.trns = st + 8;
        ctx.trns_len = len;
      }
    }
    if (!ctx.plte) return false;  // malformed palette PNG
    ctx.rs_channels = ctx.trns ? 4 : 3;
    out_ct = ctx.trns ? 6 : 2;
    out_bd = 8;
  } else {  // gray / gray+alpha / RGB / RGBA (8 or 16-bit)
    ctx.rs_channels = in.channels;
  }

  ctx.raw_row.resize(in.row_bytes);
  if (ctx.palette)
    ctx.sample_row.resize((size_t)in.width * ctx.rs_channels);  // 8-bit RGB(A)
  else if (ctx.swap16)
    ctx.sample_row.resize((size_t)in.width * ctx.rs_channels * 2);  // native u16
  if (ctx.swap16) ctx.out_swap.resize((size_t)out_w * ctx.rs_channels * 2);

  PngScanlineWriter writer;
  PngImageInfo oinfo;
  oinfo.width = out_w;
  oinfo.height = out_h;
  oinfo.bit_depth = out_bd;
  oinfo.color_type = out_ct;
  if (!writer.Begin(oinfo)) return false;
  ctx.writer = &writer;

  const stbir_datatype dtype =
      (ctx.bps == 2) ? STBIR_TYPE_UINT16
                     : (srgb ? STBIR_TYPE_UINT8_SRGB : STBIR_TYPE_UINT8);
  STBIR_RESIZE rs;
  stbir_resize_init(&rs, nullptr, (int)in.width, (int)in.height, 0, nullptr,
                    (int)out_w, (int)out_h, 0,
                    (stbir_pixel_layout)ctx.rs_channels, dtype);
  stbir_set_pixel_callbacks(&rs, ResizeInputCb, ResizeOutputCb);
  stbir_set_user_data(&rs, &ctx);
  int ok = stbir_resize_extended(&rs);
  if (!ok || ctx.error || ctx.out_rows != out_h || !reader.ok()) return false;
  return writer.Finish(out);
}

bool TranscodePNG(const uint8_t *data, size_t size, std::vector<uint8_t> &out) {
  PngScanlineReader reader;
  if (!reader.Open(data, size)) return false;
  PngScanlineWriter writer;
  if (!writer.BeginPassthrough(reader)) return false;

  std::vector<uint8_t> row(reader.info().row_bytes);
  for (uint32_t y = 0; y < reader.info().height; ++y) {
    if (!reader.NextRow(row.data())) return false;
    if (!writer.WriteRow(row.data())) return false;
  }
  if (!reader.ok()) return false;
  return writer.Finish(out);
}

}  // namespace imageio
}  // namespace tinyusdz
