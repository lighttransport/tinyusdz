# Vendored jemalloc (source + license only)

- **Upstream:** https://github.com/jemalloc/jemalloc
- **Version:** 5.3.0 (release tarball `jemalloc-5.3.0.tar.bz2`)
- **License:** 2-clause BSD — see [`COPYING`](./COPYING).

This is a **source-only** vendored copy for provenance. It is **not** compiled by
the tinyusdz/next build. The `next` CLI tools link a system or prebuilt jemalloc
via the CMake option `TINYUSDZ_NEXT_WITH_TCMALLOC` (which searches for jemalloc /
tcmalloc / mimalloc); see `src/next/CMakeLists.txt` and `doc/large-scene.md` §8.1.

jemalloc (with decay-purging disabled — `dirty_decay_ms:-1,muzzy_decay_ms:-1`,
baked into the tool via jemalloc's `malloc_conf` global) is the fastest allocator
measured for the compose+flatten workload (Caldera −20% vs tcmalloc at lower RSS).

Excluded from the vendored copy (not source, not license): `test/`, `doc/`,
`doc_internal/`. To build from this tree: `./autogen.sh && ./configure && make`.
