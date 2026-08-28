#!/usr/bin/env bash
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
ue_root="${UE_ROOT:-/mnt/nvme02/local/ue}"
output_dir="${1:-$script_dir/output}"
editor="$ue_root/Engine/Binaries/Linux/UnrealEditor-Cmd"

if [ ! -x "$editor" ]; then
  echo "ERROR: UnrealEditor-Cmd was not found at $editor" >&2
  exit 2
fi

mkdir -p "$output_dir"
mkdir -p "$script_dir/Saved/xdg-config" "$script_dir/Saved/xdg-cache"
export XDG_CONFIG_HOME="$script_dir/Saved/xdg-config"
export XDG_CACHE_HOME="$script_dir/Saved/xdg-cache"
env "UE-LocalDataCachePath=$script_dir/Saved/DerivedDataCache" \
  "$editor" "$script_dir/TinyUSDZMetaHuman.uproject" \
  -Unattended -NoSplash -NoSound -NullRHI -NoSourceControl \
  -ddc=InstalledNoZenLocalFallback \
  -LocalDataCachePath="$script_dir/Saved/DerivedDataCache" \
  -ExecutePythonScript="$script_dir/Scripts/export_metahuman.py" \
  -TinyUSDZOutput="$(realpath "$output_dir")"

python3 "$script_dir/Scripts/postprocess_usd.py" \
  --output-dir "$(realpath "$output_dir")"
