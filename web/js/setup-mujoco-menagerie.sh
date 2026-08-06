#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOCK_FILE="$SCRIPT_DIR/tests/fixtures/mujoco-menagerie.lock"
DEFAULT_DIR="$SCRIPT_DIR/.cache/mujoco_menagerie"
DATASET_DIR="${MUJOCO_MENAGERIE:-$DEFAULT_DIR}"

usage() {
  sed -n '2,24p' "$0"
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ ! -f "$LOCK_FILE" ]]; then
  echo "missing Menagerie lock file: $LOCK_FILE" >&2
  exit 2
fi

REPOSITORY="$(sed -n 's/^repository=//p' "$LOCK_FILE")"
COMMIT="$(sed -n 's/^commit=//p' "$LOCK_FILE")"
if [[ ! "$REPOSITORY" =~ ^https://github\.com/google-deepmind/mujoco_menagerie\.git$ ||
      ! "$COMMIT" =~ ^[0-9a-f]{40}$ ]]; then
  echo "invalid Menagerie lock file: $LOCK_FILE" >&2
  exit 2
fi

if [[ -e "$DATASET_DIR" && ! -d "$DATASET_DIR/.git" ]]; then
  echo "dataset path exists but is not a git checkout: $DATASET_DIR" >&2
  exit 2
fi

if [[ ! -d "$DATASET_DIR/.git" ]]; then
  mkdir -p "$(dirname "$DATASET_DIR")"
  echo "Cloning MuJoCo Menagerie into $DATASET_DIR"
  git clone --no-tags "$REPOSITORY" "$DATASET_DIR"
fi

git -C "$DATASET_DIR" fetch --no-tags --quiet origin "$COMMIT"
git -C "$DATASET_DIR" checkout --quiet --detach "$COMMIT"

ACTUAL="$(git -C "$DATASET_DIR" rev-parse HEAD)"
if [[ "$ACTUAL" != "$COMMIT" ]]; then
  echo "Menagerie checkout mismatch: expected $COMMIT, got $ACTUAL" >&2
  exit 1
fi

if [[ ! -d "$DATASET_DIR/unitree_go2" ]]; then
  echo "Menagerie checkout is missing expected model directories: $DATASET_DIR" >&2
  exit 1
fi

echo "MuJoCo Menagerie ready: $DATASET_DIR"
echo "revision: $ACTUAL"
