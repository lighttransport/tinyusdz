// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.

#pragma once

#include "value-types.hh"

namespace tinyusdz {

value::matrix4d to_matrix(const value::quath &q);
value::matrix4d to_matrix(const value::quatf &q);
value::matrix4d to_matrix(const value::quatd &q);


} // namespace tinyusdz
