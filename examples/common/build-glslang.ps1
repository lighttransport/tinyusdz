# Clone + build the Khronos glslang reference compiler and install it locally
# to examples/common/glslang. tusdview's Vulkan ray-tracing shaders need a
# glslang new enough to understand GL_EXT_ray_query (raytrace.comp) -- many
# system/Vulkan-SDK-bundled glslangValidator builds predate it. The
# compute-BVH fallback shader (raytrace_swbvh.comp) needs no ray-query
# support, so it will build with an older glslang too, but running this once
# covers both.
#
# Windows PowerShell equivalent of build-glslang.sh (Linux-only). After this
# succeeds, vk/vulkan.cmake auto-detects examples/common/glslang/bin in
# preference to a system compiler -- no other configuration needed. Or run
# vk/shaders/build-shaders.ps1 (or the .sh, under Git Bash/WSL) directly with
# this glslang to regenerate the embedded SPIR-V headers.
#
# Requires: git, cmake, a C++17 compiler (Visual Studio 2019+ / MSVC), python.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File build-glslang.ps1 [-GlslangRef 14.3.0] [-Force]
#
[CmdletBinding()]
param(
    [string]$GlslangRef = "14.3.0",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Src = Join-Path $Here "glslang-src"
$BuildDir = Join-Path $Src "build"
$Prefix = Join-Path $Here "glslang"

function Find-GlslangBinary {
    param([string]$BinDir)
    foreach ($name in @("glslangValidator.exe", "glslang.exe")) {
        $candidate = Join-Path $BinDir $name
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

function Require-Tool {
    param([string]$Name)
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        Write-Error "error: '$Name' not found in PATH"
        exit 1
    }
}

$existing = Find-GlslangBinary (Join-Path $Prefix "bin")
if ($existing -and -not $Force) {
    Write-Host "glslang already installed: $existing"
    & $existing --version
    exit 0
}

Require-Tool git
Require-Tool cmake
$python = $null
foreach ($candidate in @("python", "python3")) {
    if (Get-Command $candidate -ErrorAction SilentlyContinue) { $python = $candidate; break }
}
if (-not $python) {
    Write-Error "error: 'python' or 'python3' not found in PATH"
    exit 1
}

# Fetch the source (shallow). Fall back to the default branch if the tag is absent.
if (-not (Test-Path (Join-Path $Src ".git"))) {
    Write-Host "==> cloning glslang ($GlslangRef) ..."
    & git clone --depth 1 --branch $GlslangRef `
        https://github.com/KhronosGroup/glslang.git $Src
    if ($LASTEXITCODE -ne 0) {
        Write-Host "    (tag '$GlslangRef' not found; cloning default branch instead)"
        & git clone --depth 1 https://github.com/KhronosGroup/glslang.git $Src
        if ($LASTEXITCODE -ne 0) { Write-Error "error: git clone failed"; exit 1 }
    }
}

# Pull in the in-tree SPIRV-Tools/headers glslang expects (no system deps needed).
Write-Host "==> fetching glslang's bundled sources ..."
Push-Location $Src
try {
    & $python update_glslang_sources.py
    if ($LASTEXITCODE -ne 0) {
        Write-Host "    (update_glslang_sources.py failed; continuing with ENABLE_OPT=0)"
    }
} finally {
    Pop-Location
}

Write-Host "==> configuring + building (install prefix: $Prefix) ..."
& cmake -S $Src -B $BuildDir `
    -DENABLE_OPT=0 `
    -DGLSLANG_TESTS=OFF `
    -DENABLE_CTEST=OFF `
    "-DCMAKE_INSTALL_PREFIX=$Prefix"
if ($LASTEXITCODE -ne 0) { Write-Error "error: cmake configure failed"; exit 1 }

$jobs = if ($env:NUMBER_OF_PROCESSORS) { $env:NUMBER_OF_PROCESSORS } else { "4" }
& cmake --build $BuildDir --config Release --target install -j $jobs
if ($LASTEXITCODE -ne 0) { Write-Error "error: cmake build failed"; exit 1 }

$bin = Find-GlslangBinary (Join-Path $Prefix "bin")
if (-not $bin) {
    Write-Error "error: build finished but no glslang binary at $Prefix\bin"
    exit 1
}
Write-Host "==> installed: $bin"
& $bin --version

# Quick self-check: can it compile tusdview's two RT compute shaders?
$tusdviewShaders = Join-Path $Here "..\tusdview\vk\shaders"
$rayQueryShader = Join-Path $tusdviewShaders "raytrace.comp"
$swBvhShader = Join-Path $tusdviewShaders "raytrace_swbvh.comp"
$tmpSpv = Join-Path $env:TEMP "_rq_check.spv"

if (Test-Path $rayQueryShader) {
    & $bin -V --target-env vulkan1.2 -o $tmpSpv $rayQueryShader *> $null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "==> OK: this glslang compiles GL_EXT_ray_query (raytrace.comp)."
        Remove-Item -Force -ErrorAction SilentlyContinue $tmpSpv
    } else {
        Write-Host "==> WARNING: this glslang still cannot compile $rayQueryShader -- try a newer -GlslangRef."
    }
}
if (Test-Path $swBvhShader) {
    & $bin -V --target-env vulkan1.1 -o $tmpSpv $swBvhShader *> $null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "==> OK: this glslang compiles the compute-BVH fallback (raytrace_swbvh.comp)."
        Remove-Item -Force -ErrorAction SilentlyContinue $tmpSpv
    } else {
        Write-Host "==> WARNING: this glslang cannot compile $swBvhShader (unexpected -- it needs no RT extensions)."
    }
}

Write-Host "==> Reconfigure tusdview (cmake ..) to pick this glslang up, or run"
Write-Host "    vk/shaders/build-shaders.sh with it to regenerate embedded/*.spv.h."
