// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment, Inc.
//
// tusdzconvert: convert USD(A/C/Z) to an ARKit-friendly USDZ, with texture
// resize / re-encode (fast PNG via fpnge) and composition flatten. Also a
// standalone texture channel-repacking mode.
//
// CLI mirrors Apple's `usdzconvert` core flags where it makes sense.
//
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "arg-parser.hh"
#include "str-util.hh"
#include "io-util.hh"
#include "usdz-convert.hh"

using namespace tinyusdz;

namespace {

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

image::PngEncoder ParsePngEncoder(const std::string &s) {
  std::string v = to_lower(s);
  if (v == "fpng") return image::PngEncoder::Fpng;
  if (v == "fpnge") return image::PngEncoder::Fpnge;
  return image::PngEncoder::Auto;
}

usdz::OutputTextureFormat ParseTextureFormat(const std::string &s) {
  std::string v = to_lower(s);
  if (v == "png") return usdz::OutputTextureFormat::PNG;
  if (v == "jpeg" || v == "jpg") return usdz::OutputTextureFormat::JPEG;
  return usdz::OutputTextureFormat::KeepOriginal;
}

// Parse "file.png:CH" or "const:VALUE".
usdz::RepackChannel ParseChannelSrc(const std::string &s) {
  usdz::RepackChannel rc;
  if (startsWith(s, "const:")) {
    rc.input_file.clear();
    rc.constant = uint8_t(std::atoi(s.substr(6).c_str()) & 0xff);
    return rc;
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
      rc.input_file = s.substr(0, pos);
      rc.channel = std::atoi(tail.c_str());
      return rc;
    }
  }
  rc.input_file = s;
  rc.channel = 0;
  return rc;
}

int RunRepack(const argparser::ArgParser &parser, image::PngEncoder enc) {
  std::string outImg;
  parser.get("-repack", outImg);

  usdz::RepackSpec spec;
  int explicit_channels = 0;
  std::string s;
  if (parser.is_set("-packR")) { parser.get("-packR", s); spec.r = ParseChannelSrc(s); explicit_channels = (std::max)(explicit_channels, 1); }
  if (parser.is_set("-packG")) { parser.get("-packG", s); spec.g = ParseChannelSrc(s); explicit_channels = (std::max)(explicit_channels, 2); }
  if (parser.is_set("-packB")) { parser.get("-packB", s); spec.b = ParseChannelSrc(s); explicit_channels = (std::max)(explicit_channels, 3); }
  if (parser.is_set("-packA")) { parser.get("-packA", s); spec.a = ParseChannelSrc(s); explicit_channels = 4; }

  spec.out_channels = explicit_channels > 0 ? explicit_channels : 4;
  if (parser.is_set("-packChannels")) {
    std::string c;
    parser.get("-packChannels", c);
    spec.out_channels = std::atoi(c.c_str());
  }

  if (parser.is_set("-packSize")) {
    std::string sz;
    parser.get("-packSize", sz);
    auto x = sz.find('x');
    if (x != std::string::npos) {
      spec.out_width = std::atoi(sz.substr(0, x).c_str());
      spec.out_height = std::atoi(sz.substr(x + 1).c_str());
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
    enc = ParsePngEncoder(e);
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
    parser.get("-metersPerUnit", m);
    opts.metersPerUnit = m;
  }
  if (parser.is_set("-upAxis")) {
    std::string a;
    parser.get("-upAxis", a);
    a = to_lower(a);
    if (a == "x") opts.upAxis = Axis::X;
    else if (a == "y") opts.upAxis = Axis::Y;
    else if (a == "z") opts.upAxis = Axis::Z;
  }
  if (parser.is_set("-url")) parser.get("-url", opts.url);
  if (parser.is_set("-copyright")) parser.get("-copyright", opts.copyright);
  if (parser.is_set("-resizeTextures")) {
    std::string n;
    parser.get("-resizeTextures", n);
    opts.max_texture_size = std::atoi(n.c_str());
  }
  if (parser.is_set("-textureFormat")) {
    std::string f;
    parser.get("-textureFormat", f);
    opts.texture_format = ParseTextureFormat(f);
  }
  if (parser.is_set("-jpegQuality")) {
    std::string q;
    parser.get("-jpegQuality", q);
    opts.jpeg_quality = std::atoi(q.c_str());
  }

  usdz::UsdzConvertStats stats;
  std::string warn, err;
  if (!usdz::Convert(opts, &stats, &warn, &err)) {
    if (!warn.empty()) std::cerr << "WARN: " << warn << "\n";
    std::cerr << "ERROR: " << err << "\n";
    return 1;
  }
  if (!warn.empty() && verbose) std::cerr << "WARN: " << warn << "\n";

  std::cout << "Wrote: " << opts.output << " (" << stats.output_size
            << " bytes)\n"
            << "  textures: " << stats.num_textures
            << ", resized: " << stats.num_textures_resized
            << ", reencoded: " << stats.num_textures_reencoded
            << ", passthrough: " << stats.num_textures_passthrough << "\n";
  return 0;
}
