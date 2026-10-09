/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2024-Present Light Transport Entertainment Inc.
 *
 * spline.c -- C11 cubic spline segment math (hermite basis, bezier, anti-regress).
 *
 * The pure double helpers from src/spline-eval.hh. The templated evaluator that
 * walks the knot list stays C++ (it is instantiated for float and double and
 * consumes a C++ Spline<T>); these leaf routines are shared so the arithmetic
 * lives once and is available from C.
 */

#include "lightusd-core-c.h"

#include <math.h>

double lu_hermite_h00(double u) { return 2.0 * u * u * u - 3.0 * u * u + 1.0; }
double lu_hermite_h10(double u) { return u * u * u - 2.0 * u * u + u; }
double lu_hermite_h01(double u) { return -2.0 * u * u * u + 3.0 * u * u; }
double lu_hermite_h11(double u) { return u * u * u - u * u; }

/* B(u) = (1-u)^3 p0 + 3(1-u)^2 u p1 + 3(1-u) u^2 p2 + u^3 p3 */
double lu_cubic_bezier(double p0, double p1, double p2, double p3, double u) {
  const double iu = 1.0 - u;
  return iu * iu * iu * p0 + 3.0 * iu * iu * u * p1 + 3.0 * iu * u * u * p2 +
         u * u * u * p3;
}

/* B'(u) = 3[(1-u)^2 (p1-p0) + 2(1-u)u (p2-p1) + u^2 (p3-p2)] */
double lu_cubic_bezier_deriv(double p0, double p1, double p2, double p3,
                             double u) {
  const double iu = 1.0 - u;
  return 3.0 * (iu * iu * (p1 - p0) + 2.0 * iu * u * (p2 - p1) +
                u * u * (p3 - p2));
}

/* Newton-Raphson + bisection for B_time(u) = targetTime, u in [0,1]. */
double lu_solve_bezier_time_for_u(double t0, double t1, double t2, double t3,
                                  double target_time, int max_iter) {
  double u = (t3 > t0) ? (target_time - t0) / (t3 - t0) : 0.5;
  u = fmax(0.0, fmin(1.0, u));

  double lo = 0.0, hi = 1.0;
  for (int i = 0; i < max_iter; i++) {
    const double bt = lu_cubic_bezier(t0, t1, t2, t3, u);
    const double err = bt - target_time;
    if (fabs(err) < 1e-12) break;

    const double dt = lu_cubic_bezier_deriv(t0, t1, t2, t3, u);
    if (fabs(dt) > 1e-15) {
      const double u_new = u - err / dt;
      if (u_new >= lo && u_new <= hi) {
        u = u_new;
      } else {
        u = 0.5 * (lo + hi);
      }
    } else {
      u = 0.5 * (lo + hi);
    }

    const double btu = lu_cubic_bezier(t0, t1, t2, t3, u);
    if (btu < target_time) {
      lo = u;
    } else {
      hi = u;
    }
  }
  return fmax(0.0, fmin(1.0, u));
}

/* Anti-regression (Spec 12.5.3.5): shrink tangent widths if the time control
 * points would be non-monotonic. */
void lu_spline_anti_regress(double t0, double *tw0_post, double *tw1_pre,
                            double t3) {
  const double dt = t3 - t0;
  if (dt <= 0.0) {
    *tw0_post = 0.0;
    *tw1_pre = 0.0;
    return;
  }
  const double total_width = *tw0_post + *tw1_pre;
  if (total_width > dt) {
    const double scale = dt / total_width;
    *tw0_post *= scale;
    *tw1_pre *= scale;
  }
  *tw0_post = fmax(0.0, *tw0_post);
  *tw1_pre = fmax(0.0, *tw1_pre);
}
