// SPDX-License-Identifier: Apache-2.0
//
// lusdquicklook — a minimal, portable USD Quick Look viewer.
//
// Browse a folder, click a .usd* file, get a shaded preview. Runs with no GPU,
// holds a bounded memory budget, and degrades (rather than dies) on files too
// large to preview.
#include <cstdio>

#include "app.hh"
#include "options.hh"

int main(int argc, char** argv) {
  lusdql::Options opts;
  bool want_help = false;
  std::string err;

  if (!lusdql::ParseOptions(argc, argv, &opts, &want_help, &err)) {
    std::fprintf(stderr, "lusdquicklook: %s\n\n%s", err.c_str(),
                 lusdql::UsageText());
    return 2;
  }
  if (want_help) {
    std::fputs(lusdql::UsageText(), stdout);
    return 0;
  }

  lusdql::App app(opts);
  if (!app.Init()) {
    std::fprintf(stderr, "lusdquicklook: %s\n", app.LastError().c_str());
    return 1;
  }

  const int rc = opts.screenshot.empty() ? app.Run() : app.RunHeadless();
  if (rc != 0 && !app.LastError().empty()) {
    std::fprintf(stderr, "lusdquicklook: %s\n", app.LastError().c_str());
  }
  return rc;
}
