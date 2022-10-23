// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-Present Light Transport Entertainment, Inc.
#include "linear-algebra.hh"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/linalg.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace tinyusdz {

#if 0
value::quath slerp(const value::quath &a, const value::quath &b, const float t) {
  value::quatf q;
   
}
#endif

value::quatf slerp(const value::quatf &a, const value::quatf &b, const float t) {
  linalg::vec<float, 4> qa;    
  linalg::vec<float, 4> qb;    
  linalg::vec<float, 4> qret;    

  memcpy(&qa, &a, sizeof(float) * 4);
  memcpy(&qb, &b, sizeof(float) * 4);

  qret = linalg::slerp(qa, qb, t);
  
  
  return *(reinterpret_cast<value::quatf *>(&qret));
}

value::quatd slerp(const value::quatd &a, const value::quatd &b, const double t) {

  linalg::vec<double, 4> qa;    
  linalg::vec<double, 4> qb;    
  linalg::vec<double, 4> qret;    

  memcpy(&qa, &a, sizeof(double) * 4);
  memcpy(&qb, &b, sizeof(double) * 4);

  qret = linalg::slerp(qa, qb, t);
  
  return *(reinterpret_cast<value::quatd *>(&qret));

}

} // namespace tinyusdz
