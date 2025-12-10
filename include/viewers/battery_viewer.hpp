// Copyright (c) 2020 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.

#pragma once
#include "global.hpp"

// Battery viewer is only available on Inkplate boards, not on Paper S3.
#if EPUB_INKPLATE_BUILD && !BOARD_TYPE_PAPER_S3
  namespace BatteryViewer {

    #if __BATTERY_VIEWER__
      void show();
    #else
      extern void show();
    #endif

  }
#endif

