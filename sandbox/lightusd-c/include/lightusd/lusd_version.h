/*
 * lusd_version.h - Version constants
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_VERSION_H
#define LUSD_VERSION_H

#include "lusd_platform.h"

#define LUSD_VERSION_MAJOR 0
#define LUSD_VERSION_MINOR 1
#define LUSD_VERSION_PATCH 0

#define LUSD_MAKE_API_VERSION(major, minor, patch) \
    ((uint32_t)(major) << 22 | (uint32_t)(minor) << 12 | (uint32_t)(patch))

#define LUSD_API_VERSION \
    LUSD_MAKE_API_VERSION(LUSD_VERSION_MAJOR, LUSD_VERSION_MINOR, LUSD_VERSION_PATCH)

#define LUSD_API_VERSION_MAJOR(version) ((uint32_t)(version) >> 22)
#define LUSD_API_VERSION_MINOR(version) (((uint32_t)(version) >> 12) & 0x3FFU)
#define LUSD_API_VERSION_PATCH(version) ((uint32_t)(version) & 0xFFFU)

#endif /* LUSD_VERSION_H */
