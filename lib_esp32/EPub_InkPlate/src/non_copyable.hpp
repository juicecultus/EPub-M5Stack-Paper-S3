// Copyright (c) 2020 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.

#pragma once

#include "global.hpp"

/**
 * @brief Make children classes impossible to be copied.
 */
class NonCopyable
{
public:
  NonCopyable(const NonCopyable &) = delete;
  NonCopyable & operator=(const NonCopyable &) = delete;

protected:
  constexpr NonCopyable() = default;
  ~NonCopyable() = default;
};
