// SPDX-License-Identifier: Apache-2.0
// tusdrender — CLI option parsing (Options) + the null asset-resolution shim.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "asset-resolution.hh"
#include "tsd/tinysubdiv.hh"
#include "tusdr_context.hh"

namespace tusdr {

bool ParseIntStrict(const std::string &s, int *out) {
  if (!out || s.empty()) return false;
  errno = 0;
  char *end = nullptr;
  long v = std::strtol(s.c_str(), &end, 10);
  if (errno == ERANGE || end != s.c_str() + s.size()) return false;
  if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max()) {
    return false;
  }
  *out = int(v);
  return true;
}

bool ParseFloatStrict(const std::string &s, float *out) {
  if (!out || s.empty()) return false;
  errno = 0;
  char *end = nullptr;
  float v = std::strtof(s.c_str(), &end);
  if (errno == ERANGE || end != s.c_str() + s.size() || !std::isfinite(v)) {
    return false;
  }
  *out = v;
  return true;
}

bool ParseDoubleStrict(const std::string &s, double *out) {
  if (!out || s.empty()) return false;
  errno = 0;
  char *end = nullptr;
  double v = std::strtod(s.c_str(), &end);
  if (errno == ERANGE || end != s.c_str() + s.size() || !std::isfinite(v)) {
    return false;
  }
  *out = v;
  return true;
}

bool ParseColor(const std::string &s, Vec3 *out) {
  if (!out) return false;
  size_t p0 = s.find(',');
  size_t p1 = (p0 == std::string::npos) ? std::string::npos : s.find(',', p0 + 1);
  if (p0 == std::string::npos || p1 == std::string::npos ||
      s.find(',', p1 + 1) != std::string::npos) {
    return false;
  }
  Vec3 v;
  if (!ParseFloatStrict(s.substr(0, p0), &v.x) ||
      !ParseFloatStrict(s.substr(p0 + 1, p1 - p0 - 1), &v.y) ||
      !ParseFloatStrict(s.substr(p1 + 1), &v.z)) {
    return false;
  }
  *out = v;
  return true;
}

void PrintUsage(const char *prog) {
  std::cout
      << "tusdrender - CPU preview raytrace renderer for USD\n\n"
      << "Usage:\n"
      << "  " << prog << " input.usd output.png [options]\n\n"
      << "Options:\n"
      << "  -h, -help              Show this help.\n"
      << "  -w, -width <N>         Output width (default 960).\n"
      << "  -height <N>            Output height (default from aspect or 540).\n"
      << "  -camera <path|name>    Camera absolute path or camera name.\n"
      << "  -fitScale <value>      Auto-fit camera distance multiplier (default 1.8).\n"
      << "  -viewDir <x,y,z>       Auto-fit camera direction from target to eye.\n"
      << "  -purpose <list>        Visible USD purposes: default,render,proxy,guide.\n"
      << "  -showGuide             Include purpose=guide geometry.\n"
      << "  -hideProxy             Hide purpose=proxy geometry.\n"
      << "  -hideRender            Hide purpose=render geometry.\n"
      << "  -hideDefault           Hide default-purpose geometry.\n"
      << "  -timecode <value>      USD timecode to evaluate.\n"
      << "  -samples <N>           Deterministic supersamples per pixel (default 1).\n"
      << "  -bg <r,g,b>            Background color in linear RGB (default 0,0,0).\n"
      << "  -ambient <value>       Ambient diffuse term (default 0.05).\n"
      << "  -noShadows             Disable hard shadow rays.\n"
      << "  -smooth                Interpolate authored normals (smooth shading)\n"
      << "                         instead of per-face geometric normals.\n"
      << "  -noDisplace            Disable UsdPreviewSurface displacement.\n"
      << "  -displaceScale <f>     Global displacement multiplier (default 1.0).\n"
      << "  -rtPreview             Use mmap zero-copy mesh preview path for large USDC.\n"
      << "  -progress              Print long-running load/build progress.\n"
      << "  -quality <fast|default|hq>\n"
      << "                         LightRT BVH build quality (default default).\n"
      << "  -threads <N>           LightRT build threads (0 = backend default).\n"
      << "  -subdiv <N>            Subdivision level for Mesh subdivisionScheme\n"
      << "                         catmullClark/loop/bilinear (default 0).\n"
      << "  -complexity <low|medium|high|veryhigh>\n"
      << "                         usdrecord refinement preset -> subdiv 0/1/2/3.\n"
      << "  -autoframe             usdrecord-style auto camera framing.\n"
      << "  -timecode <t>          Evaluate animation at time code t.\n"
      << "  -defaultTime           Evaluate at the default (non-animated) time.\n"
      << "  -frames <FRAMESPEC>    Render an animation; one image per time code.\n"
      << "                         FRAMESPEC: t | start:end | start:end x stride,\n"
      << "                         comma-separated. Output path uses # for the\n"
      << "                         frame number (e.g. frame.####.png).\n"
      << "  -mask <PATH[,PATH...]> Restrict rendering to these prim subtrees.\n"
      << "  -variant <SET=SEL>     Override variant selection (e.g.\n"
      << "                         --variant districtLod=full). Repeatable.\n"
      << "  -legacyLoad            Use the legacy eager loader (next is default).\n"
#ifdef TINYUSDZ_WITH_QJS
      << "  -js <file.js>          Drive rendering from a JavaScript script.\n"
      << "                         Scene + BVH stay resident across renders\n"
      << "                         (memory-persistent, e.g. camera animation).\n"
      << "                         API: tusdrender.{setCamera,orbit,setResolution,\n"
      << "                         setAmbient,setBackground,setShadows,setSamples,\n"
      << "                         autoframe,bounds,stats,render}.\n"
      << "  -mcp                   Run an MCP stdio control server over the\n"
      << "                         resident scene (tools: eval,set_camera,orbit,\n"
      << "                         set_resolution,render,bounds,stats).\n"
#endif
      << "  -vk                   Use the Vulkan rasterizer backend.\n"
      << "  -vkr                  Use the Vulkan ray-tracing backend.\n"
      << "  -d3d                  Use the Direct3D 11 compute backend (Windows).\n"
      << "  -noDirectPrims         Tessellate USD shapes/curves/NURBS instead of\n"
      << "                         using tusdrender direct primitive paths.\n"
      << "  -stats                 Print scene/BVH stats.\n"
      << "  -noar                  Disable external asset resolution.\n";
}

bool ParseArgs(int argc, char **argv, Options *opt) {
  if (!opt) return false;
  if (argc <= 1) {
    PrintUsage(argv[0]);
    return false;
  }
  std::vector<std::string> positional;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto need_value = [&](const char *name) -> const char * {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for " << name << "\n";
        return nullptr;
      }
      return argv[++i];
    };
    if (a == "-h" || a == "-help" || a == "--help") {
      PrintUsage(argv[0]);
      std::exit(EXIT_SUCCESS);
    } else if (a == "-w" || a == "-width" || a == "--width") {
      const char *v = need_value(a.c_str());
      if (!v || !ParseIntStrict(v, &opt->width) || opt->width <= 0) {
        std::cerr << "Invalid width.\n";
        return false;
      }
    } else if (a == "-height" || a == "--height") {
      const char *v = need_value(a.c_str());
      if (!v || !ParseIntStrict(v, &opt->height) || opt->height <= 0) {
        std::cerr << "Invalid height.\n";
        return false;
      }
    } else if (a == "-camera" || a == "--camera") {
      const char *v = need_value(a.c_str());
      if (!v) return false;
      opt->camera = v;
    } else if (a == "-fitScale" || a == "--fitScale") {
      const char *v = need_value(a.c_str());
      if (!v) return false;
      opt->fit_scale = std::max(0.05f, std::stof(v));
    } else if (a == "-viewDir" || a == "--viewDir") {
      const char *v = need_value(a.c_str());
      if (!v || !ParseColor(v, &opt->view_dir) || Length(opt->view_dir) < 1.0e-6f) {
        std::cerr << "Invalid viewDir.\n";
        return false;
      }
      opt->has_view_dir = true;
    } else if (a == "-purpose" || a == "--purpose") {
      const char *v = need_value(a.c_str());
      if (!v) return false;
      uint32_t mask = 0;
      std::string s(v);
      size_t pos = 0;
      while (pos <= s.size()) {
        size_t comma = s.find(',', pos);
        std::string tok = s.substr(pos, comma == std::string::npos
                                            ? std::string::npos
                                            : comma - pos);
        if (tok == "default") {
          mask |= kPurposeDefaultBit;
        } else if (tok == "render") {
          mask |= kPurposeRenderBit;
        } else if (tok == "proxy") {
          mask |= kPurposeProxyBit;
        } else if (tok == "guide") {
          mask |= kPurposeGuideBit;
        } else if (!tok.empty()) {
          std::cerr << "Invalid purpose token: " << tok << "\n";
          return false;
        }
        if (comma == std::string::npos) break;
        pos = comma + 1;
      }
      opt->purpose_mask = mask;
    } else if (a == "-showGuide" || a == "--showGuide") {
      opt->purpose_mask |= kPurposeGuideBit;
    } else if (a == "-hideProxy" || a == "--hideProxy") {
      opt->purpose_mask &= ~kPurposeProxyBit;
    } else if (a == "-hideRender" || a == "--hideRender") {
      opt->purpose_mask &= ~kPurposeRenderBit;
    } else if (a == "-hideDefault" || a == "--hideDefault") {
      opt->purpose_mask &= ~kPurposeDefaultBit;
    } else if (a == "-timecode" || a == "--timecode") {
      const char *v = need_value(a.c_str());
      if (!v || !ParseDoubleStrict(v, &opt->timecode)) {
        std::cerr << "Invalid timecode.\n";
        return false;
      }
    } else if (a == "-samples" || a == "--samples") {
      const char *v = need_value(a.c_str());
      if (!v || !ParseIntStrict(v, &opt->samples) || opt->samples <= 0) {
        std::cerr << "Invalid samples.\n";
        return false;
      }
    } else if (a == "-bg" || a == "--bg") {
      const char *v = need_value(a.c_str());
      if (!v || !ParseColor(v, &opt->bg)) {
        std::cerr << "Invalid background color. Expected r,g,b.\n";
        return false;
      }
    } else if (a == "-ambient" || a == "--ambient") {
      const char *v = need_value(a.c_str());
      if (!v || !ParseFloatStrict(v, &opt->ambient) || opt->ambient < 0.0f) {
        std::cerr << "Invalid ambient value.\n";
        return false;
      }
    } else if (a == "-smooth" || a == "--smooth") {
      opt->smooth = true;
    } else if (a == "-noDisplace" || a == "--noDisplace") {
      opt->displace = false;
    } else if (a == "-displaceScale" || a == "--displaceScale") {
      const char *v = need_value(a.c_str());
      if (!v || !ParseFloatStrict(v, &opt->displace_scale)) {
        std::cerr << "Invalid -displaceScale value.\n";
        return false;
      }
    } else if (a == "-noShadows" || a == "--noShadows") {
      opt->shadows = false;
    } else if (a == "-rtPreview" || a == "--rtPreview" ||
               a == "-mmapRt" || a == "--mmapRt") {
      opt->rt_preview = true;
      opt->direct_prims = false;
    } else if (a == "-legacyLoad" || a == "--legacyLoad") {
      opt->legacy_load = true;
    } else if (a == "-progress" || a == "--progress") {
      opt->progress = true;
    } else if (a == "-quality" || a == "--quality") {
      const char *v = need_value(a.c_str());
      if (!v) return false;
      std::string q = v;
      if (q == "fast") {
        opt->quality = LRT_TRI_BUILD_FAST;
      } else if (q == "default") {
        opt->quality = LRT_TRI_BUILD_DEFAULT;
      } else if (q == "hq") {
        opt->quality = LRT_TRI_BUILD_HQ;
      } else {
        std::cerr << "Invalid quality. Expected fast, default, or hq.\n";
        return false;
      }
    } else if (a == "-threads" || a == "--threads") {
      const char *v = need_value(a.c_str());
      if (!v || !ParseIntStrict(v, &opt->threads) || opt->threads < 0) {
        std::cerr << "Invalid thread count.\n";
        return false;
      }
    } else if (a == "-maxMem" || a == "--maxMem") {
      const char *v = need_value(a.c_str());
      char *end = nullptr;
      double g = v ? std::strtod(v, &end) : 0.0;
      if (!v || end == v || g < 0.0) {
        std::cerr << "Invalid -maxMem (expected GiB, e.g. -maxMem 24).\n";
        return false;
      }
      opt->max_mem_gib = g;
    } else if (a == "-env" || a == "--env") {
      const char *v = need_value(a.c_str());
      if (!v) {
        std::cerr << "Invalid -env (expected an environment-map path).\n";
        return false;
      }
      opt->env_file = v;
    } else if (a == "-subdiv" || a == "--subdiv" ||
               a == "-subdivLevel" || a == "--subdivLevel" ||
               a == "-subdivisionLevel" || a == "--subdivisionLevel") {
      const char *v = need_value(a.c_str());
      if (!v || !ParseIntStrict(v, &opt->subdivision_level) ||
          opt->subdivision_level < 0 ||
          opt->subdivision_level > tinyusdz::tsd::kMaxLevel) {
        std::cerr << "Invalid subdivision level. Expected 0.."
                  << tinyusdz::tsd::kMaxLevel << ".\n";
        return false;
      }
    } else if (a == "-complexity" || a == "--complexity") {
      // OpenUSD usdrecord refinement presets -> subdivision level
      // (low=1.0, medium=1.1, high=1.2, veryhigh=1.3 -> refine 0/1/2/3).
      const char *v = need_value(a.c_str());
      const std::string s = v ? v : "";
      if (s == "low") {
        opt->subdivision_level = 0;
      } else if (s == "medium") {
        opt->subdivision_level = 1;
      } else if (s == "high") {
        opt->subdivision_level = 2;
      } else if (s == "veryhigh") {
        opt->subdivision_level = 3;
      } else {
        std::cerr << "Invalid -complexity. Expected low|medium|high|veryhigh.\n";
        return false;
      }
    } else if (a == "-autoframe" || a == "--autoframe") {
      opt->autoframe = true;
    } else if (a == "-mask" || a == "--mask") {
      const char *v = need_value(a.c_str());
      if (!v) {
        std::cerr << "-mask requires PRIMPATH[,PRIMPATH...].\n";
        return false;
      }
      // Comma- and/or space-separated absolute prim paths.
      std::string s = v;
      for (char &ch : s) {
        if (ch == ',') ch = ' ';
      }
      std::istringstream iss(s);
      std::string p;
      while (iss >> p) {
        if (!p.empty()) opt->mask.push_back(p);
      }
    } else if (a == "-frames" || a == "--frames" || a == "-f") {
      const char *v = need_value(a.c_str());
      if (!v) {
        std::cerr << "-frames requires a FRAMESPEC.\n";
        return false;
      }
      opt->frames = v;
    } else if (a == "-defaultTime" || a == "--defaultTime") {
      opt->default_time = true;
    } else if (a == "-variant" || a == "--variant") {
      const char *v = need_value(a.c_str());
      if (!v) return false;
      std::string s = v;
      size_t eq = s.find('=');
      if (eq == std::string::npos || eq == 0 || eq + 1 >= s.size()) {
        std::cerr << "Invalid --variant. Expected SET=SELECTION (e.g. "
                     "--variant districtLod=full).\n";
        return false;
      }
      opt->variant_overrides[s.substr(0, eq)] = s.substr(eq + 1);
    } else if (a == "-vk" || a == "--vk") {
      opt->vulkan = true;
    } else if (a == "-vkr" || a == "--vkr") {
      opt->vulkan = true;
      opt->vulkan_rt = true;
    } else if (a == "-d3d" || a == "--d3d" || a == "-dx" || a == "--dx") {
      opt->use_d3d = true;
    } else if (a == "-js" || a == "--js") {
      const char *v = need_value(a.c_str());
      if (!v) {
        std::cerr << "-js requires a script file path.\n";
        return false;
      }
      opt->js_script = v;
      opt->rt_preview = true;
      opt->direct_prims = false;
    } else if (a == "-mcp" || a == "--mcp") {
      opt->mcp = true;
      opt->rt_preview = true;
      opt->direct_prims = false;
    } else if (a == "-noDirectPrims" || a == "--noDirectPrims") {
      opt->direct_prims = false;
    } else if (a == "-stats" || a == "--stats") {
      opt->stats = true;
    } else if (a == "-noar" || a == "--noar") {
      opt->no_assetresolver = true;
    } else if (!a.empty() && a[0] == '-') {
      std::cerr << "Unknown option: " << a << "\n";
      return false;
    } else {
      positional.push_back(a);
    }
  }
  // -js / -mcp drive output paths from the script / MCP calls, so only the
  // input is required there; an output positional is optional.
  const bool output_optional = opt->mcp || !opt->js_script.empty();
  if (positional.empty() || positional.size() > 2 ||
      (!output_optional && positional.size() != 2)) {
    PrintUsage(argv[0]);
    return false;
  }
  opt->input = positional[0];
  opt->output = positional.size() > 1 ? positional[1] : std::string();
  return true;
}

int NullARResolve(const char *, const std::vector<std::string> &, std::string *,
                  std::string *, void *) {
  return -1;
}

int NullARSize(const char *, uint64_t *, std::string *, void *) { return -1; }

int NullARRead(const char *, uint64_t, uint8_t *, uint64_t *, std::string *,
               void *) {
  return -1;
}

void SetupNullAssetResolution(tinyusdz::AssetResolutionResolver *resolver) {
  if (!resolver) return;
  tinyusdz::AssetResolutionHandler handler;
  handler.resolve_fun = NullARResolve;
  handler.size_fun = NullARSize;
  handler.read_fun = NullARRead;
  handler.write_fun = nullptr;
  handler.userdata = nullptr;
  resolver->register_wildcard_asset_resolution_handler(handler);
}

}  // namespace tusdr
