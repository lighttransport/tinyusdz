// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - next_usdcat
//
// Minimal tusdcat-compatible CLI for the src/next module, so the usd-wg
// asset-corpus regression gate (tests/parse-asset-corpus.mjs) can drive next
// the same way it drives the legacy loader:
//   next_usdcat [-l|-f] file.usd[acz]
//     -l  load only (parse the single layer; default)
//     -f  flatten (compose sublayers/references/payloads/... via pcp, print USDA)
// Contract: exit 0 = loaded; diagnostics on stderr prefixed "WARN : " / "ERR : ".

#include <cstdio>
#include <cstring>
#include <string>

#include "next/pcp/cache.hh"
#include "next/resolver/asset-resolver.hh"
#include "next/stage/stage.hh"
#include "next/tinyusdz-next.hh"
#include "next/writer/usda-writer.hh"

using namespace tinyusdz::next;

static void emit_lines(const std::string &msgs, const char *prefix) {
  size_t pos = 0;
  while (pos < msgs.size()) {
    size_t nl = msgs.find('\n', pos);
    if (nl == std::string::npos) nl = msgs.size();
    if (nl > pos) {
      std::fprintf(stderr, "%s%.*s\n", prefix, int(nl - pos), msgs.c_str() + pos);
    }
    pos = nl + 1;
  }
}

int main(int argc, char **argv) {
  bool flatten = false;
  const char *filename = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "-f") == 0) {
      flatten = true;
    } else if (std::strcmp(argv[i], "-l") == 0) {
      flatten = false;
    } else {
      filename = argv[i];
    }
  }
  if (!filename) {
    std::fprintf(stderr, "Usage: next_usdcat [-l|-f] file.usd[acz]\n");
    return 2;
  }

  std::string warn, err;
  Stage stage;
  bool ok = false;
  if (flatten) {
    AssetResolver resolver;
    pcp::CompositionOptions opts;
    ok = pcp::ComposeStageFromFile(filename, resolver, &stage, opts, &warn, &err);
  } else {
    ok = LoadUSD(filename, &stage, &warn, &err);
  }

  emit_lines(warn, "WARN : ");
  if (!ok) {
    emit_lines(err.empty() ? std::string("load failed") : err, "ERR : ");
    return 1;
  }
  // Composition errors are accumulated non-fatally; surface them as warnings
  // in flatten mode (the file still loaded).
  if (!err.empty()) emit_lines(err, "WARN : ");

  if (flatten) {
    std::string usda = WriteUSDAToString(stage);
    std::fwrite(usda.data(), 1, usda.size(), stdout);
  } else {
    std::printf("loaded: %zu prims\n", stage.GetStats().prim_count);
  }
  return 0;
}
