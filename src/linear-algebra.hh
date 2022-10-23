// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-Present Light Transport Entertainment, Inc.
#pragma once

#include "value-types.hh"

namespace tinyusdz {

value::quath slerp(const value::quath &a, const value::quath &b, const float t);
value::quatf slerp(const value::quatf &a, const value::quatf &b, const float t);
value::quatd slerp(const value::quatd &a, const value::quatd &b, const double t);

};
