/* SPDX-License-Identifier: BSD-3-Clause
 * Minimal bounded Ptex reader for C11 tools.
 */
#ifndef TINYEXR_PTEX_H_
#define TINYEXR_PTEX_H_
#include <stddef.h>
#include <stdint.h>
typedef struct tinyexr_ptex_info { uint16_t channels, levels; uint32_t faces; } tinyexr_ptex_info;
typedef struct tinyexr_ptex_face { uint32_t width, height, channels; uint8_t *pixels; } tinyexr_ptex_face;
int tinyexr_ptex_info_memory(const uint8_t *data, size_t size, tinyexr_ptex_info *info);
int tinyexr_ptex_read_memory(const uint8_t *data, size_t size, uint32_t face,
                             uint32_t level, size_t max_bytes,
                             tinyexr_ptex_face *out);
void tinyexr_ptex_free(tinyexr_ptex_face *face);
#endif
