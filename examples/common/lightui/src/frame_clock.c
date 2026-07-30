/*
 * src/frame_clock.c — Per-frame render budget and deadline
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* clock_gettime requires POSIX.1b or later. */
#if !defined(_WIN32) && !defined(__APPLE__)
#  if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 199309L
#    undef  _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 199309L
#  endif
#endif

#include <lightui/frame_clock.h>
#include <string.h>

/* ---- Platform time -------------------------------------------------------- */

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
static int64_t lui__now_us(void)
{
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (int64_t)(cnt.QuadPart * 1000000LL / freq.QuadPart);
}
#elif defined(__APPLE__) || defined(__linux__) || defined(__unix__)
#  include <time.h>
static int64_t lui__now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + (int64_t)ts.tv_nsec / 1000LL;
}
#else
static int64_t lui__now_us(void) { return 0; }
#endif

/* ---- Public API ----------------------------------------------------------- */

void lui_frame_clock_begin(lui_frame_clock_t *clk, int64_t budget_us)
{
    memset(clk, 0, sizeof(*clk));
    clk->start_us    = lui__now_us();
    clk->deadline_us = budget_us > 0 ? clk->start_us + budget_us : 0;
}

int lui_frame_clock_expired(lui_frame_clock_t *clk)
{
    if (clk->expired) return 1;
    if (clk->deadline_us == 0) return 0;   /* no budget set */
    if (lui__now_us() >= clk->deadline_us) {
        clk->expired = 1;
        return 1;
    }
    return 0;
}

int64_t lui_frame_clock_remaining_us(lui_frame_clock_t *clk)
{
    if (clk->deadline_us == 0 || clk->expired) return 0;
    int64_t rem = clk->deadline_us - lui__now_us();
    return rem > 0 ? rem : 0;
}

int64_t lui_frame_clock_elapsed_us(lui_frame_clock_t *clk)
{
    return lui__now_us() - clk->start_us;
}

void lui_frame_clock_end(lui_frame_clock_t *clk)
{
    clk->end_us     = lui__now_us();
    clk->elapsed_us = clk->end_us - clk->start_us;
}
