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

# UnrealEditor-Cmd does not build project plugins on demand.  Compile the
# small HairDescription-to-USD bridge on a fresh checkout before launching the
# commandlet; generated Binaries/Intermediate directories are gitignored.
plugin_binary="$script_dir/Plugins/LightUSDGroomExport/Binaries/Linux/libUnrealEditor-LightUSDGroomExport.so"
if [ ! -f "$plugin_binary" ] || find "$script_dir/Plugins/LightUSDGroomExport/Source" -type f -newer "$plugin_binary" -print -quit | grep -q .; then
  "$ue_root/Engine/Build/BatchFiles/Linux/Build.sh" UnrealEditor Linux Development \
    -Project="$script_dir/LightUSDMetaHuman.uproject" -WaitMutex
fi

mkdir -p "$output_dir"
mkdir -p "$script_dir/Saved/xdg-config" "$script_dir/Saved/xdg-cache"
export XDG_CONFIG_HOME="$script_dir/Saved/xdg-config"
export XDG_CACHE_HOME="$script_dir/Saved/xdg-cache"
extra_args=()
if [ "${LIGHTUSD_UE_AUTORIG:-0}" = "1" ]; then
  extra_args+=( -LightUSDAutoRig )
fi
env "UE-LocalDataCachePath=$script_dir/Saved/DerivedDataCache" \
  "$editor" "$script_dir/LightUSDMetaHuman.uproject" \
  -Unattended -NoSplash -NoSound -NullRHI -NoSourceControl \
  -ddc=InstalledNoZenLocalFallback \
  -LocalDataCachePath="$script_dir/Saved/DerivedDataCache" \
  -ExecutePythonScript="$script_dir/Scripts/export_metahuman.py" \
  -LightUSDOutput="$(realpath "$output_dir")" \
  "${extra_args[@]}"

python3 "$script_dir/Scripts/postprocess_usd.py" \
  --output-dir "$(realpath "$output_dir")"
