#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/build-openusd-usdcat.sh [--dry-run] [--prepare-only] [--full]

Clone/update a local OpenUSD checkout and build a minimal usdcat install for
TinyUSDZ comparison tests.

Environment overrides:
  ROOT_DIR             Repository root (default: git top-level)
  OPENUSD_REMOTE       Fork remote (default: https://github.com/lighttransport/openusd.git)
  OPENUSD_UPSTREAM     Upstream remote (default: https://github.com/PixarAnimationStudios/OpenUSD.git)
  OPENUSD_REF          Release tag/ref to build (default: v26.05)
  OPENUSD_BRANCH       Local branch to checkout (default: tinyusdz-openusd-v26.05)
  OPENUSD_SRC_DIR      Source checkout (default: $ROOT_DIR/ref/openusd)
  OPENUSD_BUILD_DIR    build_usd.py build dir (default: $ROOT_DIR/ref/openusd-build)
  OPENUSD_INSTALL_DIR  Install prefix (default: $ROOT_DIR/ref/dist)
  OPENUSD_FETCH        Set to 0 to skip network fetch operations (default: 1)
  JOBS                 Build parallelism (default: nproc/getconf, fallback 8)
  PYTHON               Python executable (default: python3)
  OPENUSD_ALLOW_DIRTY  Allow existing dirty tracked checkout when nonzero
  OPENUSD_EXTRA_ARGS   Extra args passed to build_usd.py
  OPENUSD_RETRY_NO_PYSIDE  Retry full build without PySide-dependent targets when PySide is unavailable (default: 1)

By default the build disables optional packages and builds only the tools needed
for usdcat comparisons. Pass --full to use OpenUSD defaults (plus required
tools) and install to OPENUSD_INSTALL_DIR.

When --full is used, the script retries with a "full-core" profile if the
default run fails due missing PySide UI compiler. That fallback disables USDView,
Imaging, MaterialX, and other optional modules but still builds shared libs and
`usdcat`.

The script never pushes to OPENUSD_REMOTE. If the fork does not expose
OPENUSD_REF, it fetches the ref from OPENUSD_UPSTREAM into the local checkout.
EOF
}

DRY_RUN=0
PREPARE_ONLY=0
BUILD_MODE=minimal
for arg in "$@"; do
  case "$arg" in
    -h|--help)
      usage
      exit 0
      ;;
    -n|--dry-run)
      DRY_RUN=1
      ;;
    --prepare-only)
      PREPARE_ONLY=1
      ;;
    --full)
      BUILD_MODE=full
      ;;
    *)
      echo "ERROR: unknown argument: $arg" >&2
      usage >&2
      exit 2
      ;;
  esac
done

ROOT_DIR="${ROOT_DIR:-$(git rev-parse --show-toplevel)}"
OPENUSD_REMOTE="${OPENUSD_REMOTE:-https://github.com/lighttransport/openusd.git}"
OPENUSD_UPSTREAM="${OPENUSD_UPSTREAM:-https://github.com/PixarAnimationStudios/OpenUSD.git}"
OPENUSD_REF="${OPENUSD_REF:-v26.05}"
OPENUSD_BRANCH="${OPENUSD_BRANCH:-tinyusdz-openusd-${OPENUSD_REF#v}}"
OPENUSD_SRC_DIR="${OPENUSD_SRC_DIR:-$ROOT_DIR/ref/openusd}"
OPENUSD_BUILD_DIR="${OPENUSD_BUILD_DIR:-$ROOT_DIR/ref/openusd-build}"
OPENUSD_INSTALL_DIR="${OPENUSD_INSTALL_DIR:-$ROOT_DIR/ref/dist}"
PYTHON="${PYTHON:-python3}"
OPENUSD_ALLOW_DIRTY="${OPENUSD_ALLOW_DIRTY:-0}"
OPENUSD_EXTRA_ARGS="${OPENUSD_EXTRA_ARGS:-}"
OPENUSD_FETCH="${OPENUSD_FETCH:-1}"
OPENUSD_RETRY_NO_PYSIDE="${OPENUSD_RETRY_NO_PYSIDE:-1}"

if [ -z "${JOBS:-}" ]; then
  if command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
  else
    JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '8')"
  fi
fi

run() {
  printf '+'
  printf ' %q' "$@"
  printf '\n'
  if [ "$DRY_RUN" = "0" ]; then
    "$@"
  fi
}

run_capture() {
  local log_file="$1"
  shift
  printf '+'
  printf ' %q' "$@"
  printf '\n'
  "$@" >"$log_file" 2>&1
  return $?
}

fetch_ref() {
  if [ "$OPENUSD_FETCH" = "0" ]; then
    return 1
  fi

  if [ "$DRY_RUN" = "0" ]; then
    "$@"
  else
    run "$@"
  fi
}

have_ref() {
  [ "$DRY_RUN" = "0" ] && git -C "$OPENUSD_SRC_DIR" rev-parse --verify --quiet "$1^{commit}" >/dev/null
}

if [ ! -d "$OPENUSD_SRC_DIR/.git" ]; then
  run mkdir -p "$(dirname "$OPENUSD_SRC_DIR")"
  run git clone "$OPENUSD_REMOTE" "$OPENUSD_SRC_DIR"
fi

if [ "$DRY_RUN" = "0" ] && [ ! -d "$OPENUSD_SRC_DIR/.git" ]; then
  echo "ERROR: OpenUSD checkout is not a git repository: $OPENUSD_SRC_DIR" >&2
  exit 1
fi

run git -C "$OPENUSD_SRC_DIR" remote set-url origin "$OPENUSD_REMOTE"
if [ "$DRY_RUN" = "0" ] && ! git -C "$OPENUSD_SRC_DIR" remote get-url upstream >/dev/null 2>&1; then
  run git -C "$OPENUSD_SRC_DIR" remote add upstream "$OPENUSD_UPSTREAM"
else
  run git -C "$OPENUSD_SRC_DIR" remote set-url upstream "$OPENUSD_UPSTREAM"
fi

if [ "$DRY_RUN" = "0" ] && [ "$OPENUSD_ALLOW_DIRTY" = "0" ]; then
  if [ -n "$(git -C "$OPENUSD_SRC_DIR" status --porcelain --untracked-files=no)" ]; then
    echo "ERROR: tracked changes in $OPENUSD_SRC_DIR; set OPENUSD_ALLOW_DIRTY=1 to continue" >&2
    exit 1
  fi
fi

if ! fetch_ref git -C "$OPENUSD_SRC_DIR" fetch --tags origin; then
  if [ "$DRY_RUN" = "0" ] && [ "$OPENUSD_FETCH" != "0" ]; then
    echo "WARN: failed to fetch OpenUSD tags from origin; continuing with local refs." >&2
  fi
fi

if [ "$DRY_RUN" = "0" ] && ! have_ref "$OPENUSD_REF"; then
  if [ "$OPENUSD_FETCH" != "0" ]; then
    if ! fetch_ref git -C "$OPENUSD_SRC_DIR" fetch --tags upstream "$OPENUSD_REF"; then
      if ! have_ref "$OPENUSD_REF"; then
        echo "ERROR: could not resolve $OPENUSD_REF locally; fetch from origin/upstream failed" >&2
        exit 1
      fi
    fi
  elif ! have_ref "$OPENUSD_REF"; then
    echo "ERROR: OPENUSD_REF=$OPENUSD_REF not found in local checkout and network fetch is disabled." >&2
    exit 1
  fi
fi

run git -C "$OPENUSD_SRC_DIR" checkout -B "$OPENUSD_BRANCH" "$OPENUSD_REF"

if [ "$PREPARE_ONLY" = "1" ]; then
  echo "OpenUSD checkout ready: $OPENUSD_SRC_DIR ($OPENUSD_BRANCH -> $OPENUSD_REF)"
  exit 0
fi

run mkdir -p "$OPENUSD_BUILD_DIR" "$OPENUSD_INSTALL_DIR"

BASE_BUILD_ARGS=(
  "$OPENUSD_SRC_DIR/build_scripts/build_usd.py"
  "$OPENUSD_INSTALL_DIR"
  --build "$OPENUSD_BUILD_DIR"
  --build-variant release
  --tools
)

EXTRA_ARGS=()
if [ -n "$OPENUSD_EXTRA_ARGS" ]; then
  # shellcheck disable=SC2206
  EXTRA_ARGS=($OPENUSD_EXTRA_ARGS)
fi

FULL_CORE_FALLBACK_ARGS=(
  --no-usdview
  --no-imaging
  --no-docs
  --no-tutorials
  --no-tests
  --no-examples
  --no-materialx
  --no-openvdb
  --no-openimageio
  --no-opencolorio
  --no-ptex
  --no-prman
  --no-alembic
  --no-draco
)

if [ "$BUILD_MODE" = "minimal" ]; then
  BUILD_ARGS=(
    "${BASE_BUILD_ARGS[@]}"
    --no-python
    --no-imaging
    --no-usdview
    --no-tests
    --no-examples
    --no-tutorials
    --no-docs
    --no-python-docs
    --no-materialx
    --no-openvdb
    --no-ptex
    --no-prman
    --no-openimageio
    --no-opencolorio
    --no-alembic
    --no-draco
    --build-args "USD,-DPXR_BUILD_USD_TOOLS=ON -DPXR_BUILD_USDVIEW=OFF -DPXR_BUILD_TESTS=OFF"
  )
else
  BUILD_ARGS=("${BASE_BUILD_ARGS[@]}")
fi

if [ ${#EXTRA_ARGS[@]} -gt 0 ]; then
  BUILD_ARGS+=("${EXTRA_ARGS[@]}")
fi

BUILD_ARGS+=(-j "$JOBS")

if [ "$BUILD_MODE" = "full" ]; then
  if [ "$DRY_RUN" = "0" ]; then
    build_tmp="$(mktemp)"
    if ! run_capture "$build_tmp" "$PYTHON" "${BUILD_ARGS[@]}"; then
      if [ "$OPENUSD_RETRY_NO_PYSIDE" != "0" ] && grep -q "PySide's user interface compiler was not found" "$build_tmp"; then
        echo "WARN: PySide compiler unavailable; retrying full build with full-core profile." >&2
        cat "$build_tmp"
        rm -f "$build_tmp"
        FALLBACK_ARGS=("${BASE_BUILD_ARGS[@]}" "${EXTRA_ARGS[@]}" "${FULL_CORE_FALLBACK_ARGS[@]}" -j "$JOBS")
        build_tmp="$(mktemp)"
        if ! run_capture "$build_tmp" "$PYTHON" "${FALLBACK_ARGS[@]}"; then
          cat "$build_tmp"
          rm -f "$build_tmp"
          exit 1
        fi
        cat "$build_tmp"
        rm -f "$build_tmp"
      else
        cat "$build_tmp"
        rm -f "$build_tmp"
        exit 1
      fi
    else
      cat "$build_tmp"
      rm -f "$build_tmp"
    fi
  else
    run "$PYTHON" "${BUILD_ARGS[@]}"
  fi
else
  run "$PYTHON" "${BUILD_ARGS[@]}"
fi

if [ "$DRY_RUN" = "0" ]; then
  if [ ! -x "$OPENUSD_INSTALL_DIR/bin/usdcat" ]; then
    echo "ERROR: usdcat was not installed at $OPENUSD_INSTALL_DIR/bin/usdcat" >&2
    exit 1
  fi
  "$OPENUSD_INSTALL_DIR/bin/usdcat" --help >/dev/null
  echo "OpenUSD $BUILD_MODE build ready: $OPENUSD_INSTALL_DIR"
  echo "OpenUSD usdcat ready: $OPENUSD_INSTALL_DIR/bin/usdcat"
  echo "Use: USDCAT_PATH=$OPENUSD_INSTALL_DIR/bin/usdcat tests/run-usdcat-compare.sh"
fi
