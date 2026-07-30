/*
 * lightui/frame_clock.h — Per-frame render budget and deadline
 *
 * Usage pattern:
 *
 *   lui_frame_clock_t clk;
 *   lui_frame_clock_begin(&clk, 14000);   // 14 ms budget (≈ 71 fps headroom)
 *
 *   draw_background(&clk);
 *   if (!lui_frame_clock_expired(&clk)) draw_content(&clk);
 *   if (!lui_frame_clock_expired(&clk)) draw_overlays(&clk);
 *
 *   lui_frame_clock_end(&clk);
 *   printf("frame: %lld us\n", clk.elapsed_us);
 *
 * If the deadline expires mid-frame, the partially-rendered surface is still
 * valid and can be presented (accepting visible tearing if layers are blended
 * bottom-to-top).  The next frame re-composites from where it left off via
 * the dirty-region system.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_FRAME_CLOCK_H
#define LIGHTUI_FRAME_CLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int64_t start_us;     /* frame start (monotonic µs) */
    int64_t deadline_us;  /* absolute deadline (start_us + budget_us) */
    int64_t end_us;       /* set by frame_clock_end(); 0 = not ended yet */
    int64_t elapsed_us;   /* set by frame_clock_end() */
    int     expired;      /* 1 once deadline detected (latches) */
} lui_frame_clock_t;

/*
 * Mark the start of a frame with the given budget in microseconds.
 * budget_us == 0 disables deadline checking (expired() always returns 0).
 */
void lui_frame_clock_begin(lui_frame_clock_t *clk, int64_t budget_us);

/*
 * Returns 1 if the deadline has passed (result latches after first true).
 * Calling this is cheap (single clock_gettime + compare).
 */
int  lui_frame_clock_expired(lui_frame_clock_t *clk);

/* Remaining budget in microseconds (0 if expired or budget == 0). */
int64_t lui_frame_clock_remaining_us(lui_frame_clock_t *clk);

/* Elapsed time since frame_begin, in microseconds. */
int64_t lui_frame_clock_elapsed_us(lui_frame_clock_t *clk);

/*
 * Mark the end of the frame.  Fills clk->elapsed_us.
 * May be called even if expired.
 */
void lui_frame_clock_end(lui_frame_clock_t *clk);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_FRAME_CLOCK_H */
