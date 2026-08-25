// SPDX-License-Identifier: Apache-2.0
#pragma once

/* Canonical packed GPU material ABI shared by viewer and headless renderer
 * host code. Runtime-compiled CUDA/HIP and GLSL sources mirror these named
 * values; check_gpu_material_abi.py verifies those mirrors in CTest. */
#define TUSD_GPU_OPENPBR_FLOATS 80
#define TUSD_GPU_MATERIAL_TEX_PARAM_FLOATS 155
#define TUSD_GPU_MATERIAL_TEX_SLOTS 12
#define TUSD_GPU_GRAPH_OUTPUTS 48
#define TUSD_GPU_GRAPH_HEADER_FLOATS 50
#define TUSD_GPU_GRAPH_NODE_FLOATS 21
#define TUSD_GPU_GRAPH_MAX_NODES 64
#define TUSD_GPU_GRAPH_FLOATS 1394
#define TUSD_GPU_LIGHTRT_LIGHT_FLOATS 16
#define TUSD_GPU_LIGHTRT_TEXTURE_DESC_INTS 8
