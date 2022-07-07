// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
#pragma once

#include <iostream>

#include "crate-format.hh"

namespace std {

std::ostream &operator<<(std::ostream &os, const tinyusdz::crate::Index &i);

} // namespace std

namespace tinyusdz {

} // namespace tinyusdz
