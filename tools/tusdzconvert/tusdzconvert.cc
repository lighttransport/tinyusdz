// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment, Inc.
//
// tusdzconvert: convert USD(A/C/Z) to an ARKit-friendly USDZ, with texture
// resize / re-encode (fast PNG via fpnge) and composition flatten. Also a
// standalone texture channel-repacking mode.
//
// CLI mirrors Apple's `usdzconvert` core flags where it makes sense.
//
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "arg-parser.hh"
#include "str-util.hh"
#include "io-util.hh"
#include "usdz-convert.hh"

using namespace tinyusdz;

namespace {

// Safe integer parser: returns true and sets `out` on full-string parse.
// On failure (empty, trailing chars, overflow) returns false.
bool ParseIntStrict(const std::string &s, int *out) {
  if (s.empty() || out == nullptr) return false;
  errno = 0;
  char *endptr = nullptr;
  long val = std::strtol(s.c_str(), &endptr, 10);
  if (errno == ERANGE) return false;
  if (endptr != s.c_str() + s.size()) return false;
  if (val < static_cast<long>(std::numeric_limits<int>::min()) ||
      val > static_cast<long>(std::numeric_limits<int>::max())) return false;
  *out = static_cast<int>(val);
  return true;
}

bool ParseDoubleStrict(const std::string &s, double *out) {
  if (s.empty() || out == nullptr) return false;
  errno = 0;
  char *endptr = nullptr;
  double val = std::strtod(s.c_str(), &endptr);
  if (errno == ERANGE) return false;
  if (endptr != s.c_str() + s.size()) return false;
  if (!std::isfinite(val)) return false;
  *out = val;
  return true;
}

void PrintUsage(const char *prog) {
  std::printf(
      "tusdzconvert - convert USD to USDZ (texture resize/repack, flatten, fpnge)\n"
      "\n"
      "Usage:\n"
      "  %s inputFile [outputFile] [options]\n"
      "  %s -repack <outputImage> -packR <src> [-packG <src> -packB <src> -packA <src>] [options]\n"
      "\n"
      "Conversion options:\n"
      "  -h, -help                 Show this help.\n"
      "  -v, -verbose              Verbose logging.\n"
      "  -noFlatten                Do not compose/flatten before writing (default: flatten).\n"
      "  -arkitCompatible          Apply ARKit-friendly stage metadata (Y-up, etc).\n"
      "  -metersPerUnit <value>    Override stage metersPerUnit.\n"
      "  -upAxis <X|Y|Z>           Override stage up axis.\n"
      "  -url <string>             Store a URL in stage documentation.\n"
      "  -copyright <string>       Store a copyright string in stage documentation.\n"
      "  -copytextures             Accepted for compatibility (textures are always packed).\n"
      "\n"
      "Texture options:\n"
      "  -resizeTextures <N>       Cap each texture's longest edge to N pixels.\n"
      "  -textureFormat <keep|png|jpeg>  Output texture format (default: keep).\n"
      "  -pngEncoder <fpnge|fpng>  PNG encoder backend (default: fpnge when available).\n"
      "  -jpegQuality <1-100>      JPEG quality when (re-)encoding (default: 90).\n"
      "  -noReencode               Copy unmodified textures through byte-for-byte.\n"
      "\n"
      "Fit textures to a total size budget:\n"
      "  -targetTextureSize <size> Shrink all textures so their total fits <size>\n"
      "                            (e.g. 100MB, 50mb, 1048576). Implies a fit search.\n"
      "  -fitStrategy <size|quality>  Lever to meet the budget: reduce dimensions\n"
      "                            (size) or transcode to JPEG + lower quality (quality).\n"
      "  -fitMinTextureSize <N>    Smallest longest-edge allowed by the size search (default 64).\n"
      "  -fitMinQuality <1-100>    Lowest JPEG quality allowed by the quality search (default 30).\n"
      "\n"
      "Repack mode (merge channels into one image, e.g. R=gloss, G=roughness):\n"
      "  -repack <outputImage>     Enable repack mode; write packed image here.\n"
      "  -packR/-packG/-packB/-packA <src>\n"
      "                            Channel source: 'file.png:CH' (CH=0..3, default 0)\n"
      "                            or 'const:VALUE' (VALUE=0..255).\n"
      "  -packChannels <1-4>       Number of output channels (default: from -pack* flags).\n"
      "  -packSize <W>x<H>         Output size (default: max of referenced inputs).\n"
      "\n"
      "Examples:\n"
      "  %s model.usd model.usdz -arkitCompatible -resizeTextures 1024 -v\n"
      "  %s in.usdz out.usdz -textureFormat png -pngEncoder fpnge\n"
      "  %s -repack orm.png -packR ao.png:0 -packG rough.png:0 -packB metal.png:0 -packChannels 3\n",
      prog, prog, prog, prog, prog);
}

bool ParsePngEncoder(const std::string &s, image::PngEncoder *out) {
  if (out == nullptr) return false;
  std::string v = to_lower(s);
  if (v == "auto") {
    *out = image::PngEncoder::Auto;
    return true;
  }
  if (v == "fpng") {
    *out = image::PngEncoder::Fpng;
    return true;
  }
  if (v == "fpnge") {
    *out = image::PngEncoder::Fpnge;
    return true;
  }
  return false;
}

// Parse a human-friendly byte size: "100MB", "100mb", "100M", "50KB", "1048576".
// Returns 0 on parse failure.
size_t ParseByteSize(const std::string &in) {
  std::string s = in;
  // Trim spaces.
  while (!s.empty() && s.front() == ' ') s.erase(s.begin());
  while (!s.empty() && s.back() == ' ') s.pop_back();
  if (s.empty()) return 0;

  size_t i = 0;
  double num = 0.0;
  bool any = false;
  while (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i] == '.')) {
    any = true;
    i++;
  }
  if (!any) return 0;
  std::string num_part = s.substr(0, i);
  errno = 0;
  char *endptr = nullptr;
  num = std::strtod(num_part.c_str(), &endptr);
  if (errno == ERANGE) return 0;
  if (endptr != num_part.c_str() + i) return 0;  // partial parse
  if (std::isnan(num) || std::isinf(num) || num < 0.0) return 0;

  std::string unit = to_lower(s.substr(i));
  double mult = 1.0;
  if (unit.empty() || unit == "b") mult = 1.0;
  else if (unit == "k" || unit == "kb") mult = 1024.0;
  else if (unit == "m" || unit == "mb") mult = 1024.0 * 1024.0;
  else if (unit == "g" || unit == "gb") mult = 1024.0 * 1024.0 * 1024.0;
  else return 0;

  double product = num * mult;
  if (product < 0.0 || product > double(std::numeric_limits<size_t>::max())) return 0;
  return size_t(product);
}

bool ParseTextureFormat(const std::string &s, usdz::OutputTextureFormat *out) {
  if (out == nullptr) return false;
  std::string v = to_lower(s);
  if (v == "keep") {
    *out = usdz::OutputTextureFormat::KeepOriginal;
    return true;
  }
  if (v == "png") {
    *out = usdz::OutputTextureFormat::PNG;
    return true;
  }
  if (v == "jpeg" || v == "jpg") {
    *out = usdz::OutputTextureFormat::JPEG;
    return true;
  }
  return false;
}

// Parse "file.png:CH" or "const:VALUE".
bool ParseChannelSrc(const std::string &s, usdz::RepackChannel *rc) {
  if (rc == nullptr) return false;
  *rc = usdz::RepackChannel{};
  if (startsWith(s, "const:")) {
    rc->input_file.clear();
    int cv = 0;
    if (!ParseIntStrict(s.substr(6), &cv)) return false;
    if (cv < 0 || cv > 255) return false;
    rc->constant = uint8_t(cv);
    return true;
  }
  // Treat a trailing ":<digit>" as a channel selector.
  auto pos = s.rfind(':');
  if (pos != std::string::npos && pos + 1 < s.size()) {
    const std::string tail = s.substr(pos + 1);
    bool all_digit = !tail.empty();
    for (char c : tail) {
      if (c < '0' || c > '9') { all_digit = false; break; }
    }
    if (all_digit && tail.size() <= 1) {
      rc->input_file = s.substr(0, pos);
      if (!ParseIntStrict(tail, &rc->channel)) return false;
      if (rc->channel < 0 || rc->channel > 3) return false;
      return true;
    }
  }
  rc->input_file = s;
  rc->channel = 0;
  return true;
}

int RunRepack(const argparser::ArgParser &parser, image::PngEncoder enc) {
  std::string outImg;
  parser.get("-repack", outImg);

  usdz::RepackSpec spec;
  int explicit_channels = 0;
  std::string s;
  if (parser.is_set("-packR")) {
    parser.get("-packR", s);
    if (!ParseChannelSrc(s, &spec.r)) {
      std::cerr << "ERROR: invalid -packR source '" << s << "'.\n";
      return 1;
    }
    explicit_channels = (std::max)(explicit_channels, 1);
  }
  if (parser.is_set("-packG")) {
    parser.get("-packG", s);
    if (!ParseChannelSrc(s, &spec.g)) {
      std::cerr << "ERROR: invalid -packG source '" << s << "'.\n";
      return 1;
    }
    explicit_channels = (std::max)(explicit_channels, 2);
  }
  if (parser.is_set("-packB")) {
    parser.get("-packB", s);
    if (!ParseChannelSrc(s, &spec.b)) {
      std::cerr << "ERROR: invalid -packB source '" << s << "'.\n";
      return 1;
    }
    explicit_channels = (std::max)(explicit_channels, 3);
  }
  if (parser.is_set("-packA")) {
    parser.get("-packA", s);
    if (!ParseChannelSrc(s, &spec.a)) {
      std::cerr << "ERROR: invalid -packA source '" << s << "'.\n";
      return 1;
    }
    explicit_channels = 4;
  }

  spec.out_channels = explicit_channels > 0 ? explicit_channels : 4;
  if (parser.is_set("-packChannels")) {
    std::string c;
    parser.get("-packChannels", c);
    if (!ParseIntStrict(c, &spec.out_channels)) spec.out_channels = 0;
    if (spec.out_channels < 1 || spec.out_channels > 4) {
      std::cerr << "ERROR: -packChannels must be 1-4 (got '" << c << "').\n";
      return 1;
    }
  }

  if (parser.is_set("-packSize")) {
    std::string sz;
    parser.get("-packSize", sz);
    auto x = sz.find('x');
    if (x == std::string::npos || sz.find('x', x + 1) != std::string::npos) {
      std::cerr << "ERROR: -packSize must be <W>x<H> (got '" << sz << "').\n";
      return 1;
    }
    if (!ParseIntStrict(sz.substr(0, x), &spec.out_width)) spec.out_width = 0;
    if (!ParseIntStrict(sz.substr(x + 1), &spec.out_height)) spec.out_height = 0;
    if (spec.out_width <= 0 || spec.out_height <= 0) {
      std::cerr << "ERROR: -packSize dimensions must be positive (got '" << sz << "').\n";
      return 1;
    }
  }

  std::string warn, err;
  if (!usdz::RepackTextureFiles(spec, outImg, enc, &warn, &err)) {
    if (!warn.empty()) std::cerr << "WARN: " << warn << "\n";
    std::cerr << "ERROR: " << err << "\n";
    return 1;
  }
  if (!warn.empty()) std::cerr << "WARN: " << warn << "\n";
  std::cout << "Wrote packed texture: " << outImg << "\n";
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  using namespace tinyusdz::argparser;

  ArgParser parser;
  parser.add_option("-h", false, "Show help");
  parser.add_option("-help", false, "Show help");
  parser.add_option("-v", false, "Verbose");
  parser.add_option("-verbose", false, "Verbose");
  parser.add_option("-noFlatten", false, "Disable flatten");
  parser.add_option("-arkitCompatible", false, "ARKit metadata");
  parser.add_option("-metersPerUnit", true, "metersPerUnit");
  parser.add_option("-upAxis", true, "Up axis X/Y/Z");
  parser.add_option("-url", true, "URL metadata");
  parser.add_option("-copyright", true, "Copyright metadata");
  parser.add_option("-copytextures", false, "Compatibility no-op");
  parser.add_option("-resizeTextures", true, "Cap texture longest edge");
  parser.add_option("-textureFormat", true, "keep|png|jpeg");
  parser.add_option("-pngEncoder", true, "fpnge|fpng");
  parser.add_option("-jpegQuality", true, "1-100");
  parser.add_option("-noReencode", false, "Passthrough unmodified textures");
  parser.add_option("-targetTextureSize", true, "Total texture budget (e.g. 100MB)");
  parser.add_option("-fitStrategy", true, "size|quality");
  parser.add_option("-fitMinTextureSize", true, "Min longest edge for size fit");
  parser.add_option("-fitMinQuality", true, "Min JPEG quality for quality fit");
  // Repack mode.
  parser.add_option("-repack", true, "Repack output image");
  parser.add_option("-packR", true, "R channel source");
  parser.add_option("-packG", true, "G channel source");
  parser.add_option("-packB", true, "B channel source");
  parser.add_option("-packA", true, "A channel source");
  parser.add_option("-packChannels", true, "Output channels 1-4");
  parser.add_option("-packSize", true, "WxH");

  if (!parser.parse(argc, argv)) {
    std::cerr << "ERROR: failed to parse arguments.\n\n";
    PrintUsage(argv[0]);
    return 1;
  }

  if (parser.is_set("-h") || parser.is_set("-help") || argc < 2) {
    PrintUsage(argv[0]);
    return (argc < 2) ? 1 : 0;
  }

  const bool verbose = parser.is_set("-v") || parser.is_set("-verbose");
  image::PngEncoder enc = image::PngEncoder::Auto;
  if (parser.is_set("-pngEncoder")) {
    std::string e;
    parser.get("-pngEncoder", e);
    if (!ParsePngEncoder(e, &enc)) {
      std::cerr << "ERROR: -pngEncoder must be auto, fpnge, or fpng (got '"
                << e << "').\n";
      return 1;
    }
  }

  // Repack mode short-circuits the USD conversion.
  if (parser.is_set("-repack")) {
    return RunRepack(parser, enc);
  }

  const auto &pos = parser.positional();
  if (pos.empty()) {
    std::cerr << "ERROR: no input file specified.\n\n";
    PrintUsage(argv[0]);
    return 1;
  }

  usdz::UsdzConvertOptions opts;
  opts.inputs.push_back(pos[0]);

  if (pos.size() >= 2) {
    opts.output = pos[1];
  } else {
    // Derive <input-stem>.usdz next to the input.
    const std::string ext = io::GetFileExtension(pos[0]);
    std::string stem = pos[0];
    if (!ext.empty()) {
      stem = pos[0].substr(0, pos[0].size() - ext.size() - 1);
    }
    opts.output = stem + ".usdz";
  }

  opts.flatten = !parser.is_set("-noFlatten");
  opts.arkit_compatible = parser.is_set("-arkitCompatible");
  opts.verbose = verbose;
  opts.reencode = !parser.is_set("-noReencode");
  opts.png_encoder = enc;

  if (parser.is_set("-metersPerUnit")) {
    double m = 0.0;
    std::string ms;
    parser.get("-metersPerUnit", ms);
    if (!ParseDoubleStrict(ms, &m) || m <= 0.0) {
      std::cerr << "ERROR: -metersPerUnit must be a positive finite number (got '"
                << ms << "').\n";
      return 1;
    }
    opts.metersPerUnit = m;
  }
  if (parser.is_set("-upAxis")) {
    std::string a;
    parser.get("-upAxis", a);
    a = to_lower(a);
    if (a == "x") opts.upAxis = Axis::X;
    else if (a == "y") opts.upAxis = Axis::Y;
    else if (a == "z") opts.upAxis = Axis::Z;
    else {
      std::cerr << "ERROR: -upAxis must be X, Y, or Z (got '" << a << "').\n";
      return 1;
    }
  }
  if (parser.is_set("-url")) parser.get("-url", opts.url);
  if (parser.is_set("-copyright")) parser.get("-copyright", opts.copyright);
  if (parser.is_set("-resizeTextures")) {
    std::string n;
    parser.get("-resizeTextures", n);
    if (!ParseIntStrict(n, &opts.max_texture_size)) opts.max_texture_size = 0;
    if (opts.max_texture_size <= 0) {
      std::cerr << "ERROR: -resizeTextures must be a positive integer (got '" << n << "').\n";
      return 1;
    }
  }
  if (parser.is_set("-textureFormat")) {
    std::string f;
    parser.get("-textureFormat", f);
    if (!ParseTextureFormat(f, &opts.texture_format)) {
      std::cerr << "ERROR: -textureFormat must be keep, png, or jpeg (got '"
                << f << "').\n";
      return 1;
    }
  }
  if (parser.is_set("-jpegQuality")) {
    std::string q;
    parser.get("-jpegQuality", q);
    if (!ParseIntStrict(q, &opts.jpeg_quality)) opts.jpeg_quality = 0;
    if (opts.jpeg_quality < 1 || opts.jpeg_quality > 100) {
      std::cerr << "ERROR: -jpegQuality must be 1-100 (got '" << q << "').\n";
      return 1;
    }
  }
  if (parser.is_set("-targetTextureSize")) {
    std::string s;
    parser.get("-targetTextureSize", s);
    opts.target_texture_bytes = ParseByteSize(s);
    if (opts.target_texture_bytes == 0) {
      std::cerr << "ERROR: could not parse -targetTextureSize '" << s
                << "' (try e.g. 100MB).\n";
      return 1;
    }
  }
  if (parser.is_set("-fitStrategy")) {
    std::string s;
    parser.get("-fitStrategy", s);
    s = to_lower(s);
    if (s == "quality") {
      opts.fit_strategy = usdz::FitStrategy::Quality;
    } else if (s == "size") {
      opts.fit_strategy = usdz::FitStrategy::Size;
    } else {
      std::cerr << "ERROR: -fitStrategy must be size or quality (got '" << s
                << "').\n";
      return 1;
    }
  }
  if (parser.is_set("-fitMinTextureSize")) {
    std::string s;
    parser.get("-fitMinTextureSize", s);
    if (!ParseIntStrict(s, &opts.fit_min_texture_size)) opts.fit_min_texture_size = 0;
    if (opts.fit_min_texture_size < 1 || opts.fit_min_texture_size > 16384) {
      std::cerr << "ERROR: -fitMinTextureSize must be 1-16384 (got '" << s << "').\n";
      return 1;
    }
  }
  if (parser.is_set("-fitMinQuality")) {
    std::string s;
    parser.get("-fitMinQuality", s);
    if (!ParseIntStrict(s, &opts.fit_min_jpeg_quality)) opts.fit_min_jpeg_quality = 0;
    if (opts.fit_min_jpeg_quality < 1 || opts.fit_min_jpeg_quality > 100) {
      std::cerr << "ERROR: -fitMinQuality must be 1-100 (got '" << s << "').\n";
      return 1;
    }
  }

  usdz::UsdzConvertStats stats;
  std::string warn, err;
  if (!usdz::Convert(opts, &stats, &warn, &err)) {
    if (!warn.empty()) std::cerr << "WARN: " << warn << "\n";
    std::cerr << "ERROR: " << err << "\n";
    return 1;
  }
  if (!warn.empty()) std::cerr << "WARN: " << warn << "\n";

  std::cout << "Wrote: " << opts.output << " (" << stats.output_size
            << " bytes)\n"
            << "  textures: " << stats.num_textures
            << ", resized: " << stats.num_textures_resized
            << ", reencoded: " << stats.num_textures_reencoded
            << ", passthrough: " << stats.num_textures_passthrough << "\n";
  return 0;
}
