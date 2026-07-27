// SPDX-License-Identifier: Apache 2.0
// Copyright 2026 - Present, Light Transport Entertainment Inc.

#pragma once

#if defined(__clang__)
#define TINYUSDZ_LIFETIMEBOUND [[clang::lifetimebound]]
#else
#define TINYUSDZ_LIFETIMEBOUND
#endif
