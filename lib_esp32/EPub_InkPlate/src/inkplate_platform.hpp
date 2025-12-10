// Minimal InkPlatePlatform shim for ESP32 builds.
// For Inkplate boards, this header defers to the original
// Inkplate platform implementation from the ESP-IDF-Inkplate
// library. For BOARD_TYPE_PAPER_S3, it provides a stub
// implementation that we will extend to use epdiy.

#pragma once

#include "global.hpp"
#include "non_copyable.hpp"

#if defined(BOARD_TYPE_PAPER_S3)

#include "driver/gpio.h"

class InkPlatePlatform : NonCopyable
{
private:
  static constexpr char const * TAG = "InkPlatePlatform";
  static InkPlatePlatform singleton;
  InkPlatePlatform() = default;

public:
  static inline InkPlatePlatform & get_singleton() noexcept { return singleton; }

  // For now, setup/light_sleep/deep_sleep are minimal stubs.
  bool setup(bool sd_card_init = false);
  bool light_sleep(uint32_t minutes_to_sleep, gpio_num_t gpio_num = (gpio_num_t)0, int level = 1);
  void deep_sleep(gpio_num_t gpio_num = (gpio_num_t)0, int level = 1);
};

extern InkPlatePlatform & inkplate_platform;

#else

// Non-Paper S3 builds should use the original Inkplate
// platform implementation provided by ESP-IDF-Inkplate.
#include_next "inkplate_platform.hpp"

#endif
