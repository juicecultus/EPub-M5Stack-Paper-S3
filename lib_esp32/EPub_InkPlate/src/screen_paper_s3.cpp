#define __SCREEN__ 1
#include "screen.hpp"

#if defined(BOARD_TYPE_PAPER_S3)

extern "C" {
  #include <epdiy.h>
  #include <epd_highlevel.h>
  #include <epd_display.h>
}

// Board definition implemented in PaperS3Support/EpdiyPaperS3Board.c
extern "C" {
  extern const EpdBoardDefinition paper_s3_board;
}

#ifndef EPD_WIDTH
#define EPD_WIDTH 960
#endif

#ifndef EPD_HEIGHT
#define EPD_HEIGHT 540
#endif

static EpdiyHighlevelState s_hl;
static bool s_epd_initialized = false;
static uint8_t *s_framebuffer = nullptr;
static bool s_force_full = true;
static int16_t s_partial_count = 0;
static const int16_t PARTIAL_COUNT_ALLOWED = 10;
static int s_temperature = 20; // TODO: hook real temperature sensor

Screen Screen::singleton;

uint16_t Screen::width  = EPD_WIDTH;
uint16_t Screen::height = EPD_HEIGHT;

void Screen::clear()
{
  if (!s_epd_initialized) return;
  epd_hl_set_all_white(&s_hl);
}

void Screen::update(bool no_full)
{
  if (!s_epd_initialized) return;

  if (s_force_full) {
    epd_hl_update_screen(&s_hl, MODE_GC16, s_temperature);
    s_force_full = false;
    s_partial_count = PARTIAL_COUNT_ALLOWED;
    return;
  }

  if (no_full) {
    epd_hl_update_screen(&s_hl, MODE_GL16, s_temperature);
    s_partial_count = 0;
    return;
  }

  if (s_partial_count <= 0) {
    epd_hl_update_screen(&s_hl, MODE_GC16, s_temperature);
    s_partial_count = PARTIAL_COUNT_ALLOWED;
  } else {
    epd_hl_update_screen(&s_hl, MODE_GL16, s_temperature);
    s_partial_count--;
  }
}

void Screen::force_full_update()
{
  s_force_full = true;
  s_partial_count = 0;
}

void Screen::setup(PixelResolution resolution, Orientation orientation)
{
  if (!s_epd_initialized) {
    epd_set_board(&paper_s3_board);
    epd_init(epd_current_board(), &ED047TC2, EPD_OPTIONS_DEFAULT);
    // Rotate the epdiy drawing coordinates so that the logical page is
    // portrait when the device is held with USB-C at the bottom and the
    // power button on the right.
    epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
    // The C fallback for the ESP32-S3 LUT path is slower than the original
    // vector assembly, so we run the LCD at 5 MHz for stability.
    epd_set_lcd_pixel_clock_MHz(5);

    s_hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
    epd_hl_set_all_white(&s_hl);
    s_framebuffer = epd_hl_get_framebuffer(&s_hl);

    epd_poweron();
    // Ensure any previous image on the panel is fully cleared on first
    // boot so we start from a clean white screen.
    epd_fullclear(&s_hl, s_temperature);
    s_epd_initialized = true;
    s_force_full = false;
    s_partial_count = PARTIAL_COUNT_ALLOWED;
  }

  // On Paper S3 we always drive the panel in grayscale (4-bit via epdiy).
  // Ignore the stored pixel resolution setting and force the non-ONE_BIT
  // path so glyphs/bitmaps are generated as grayscale only.
  (void)resolution;
  set_pixel_resolution(PixelResolution::THREE_BITS, true);
  set_orientation(orientation);
  clear();
}

void Screen::set_pixel_resolution(PixelResolution resolution, bool force)
{
  if (force || (pixel_resolution != resolution)) {
    pixel_resolution = resolution;
  }
}

void Screen::set_orientation(Orientation orient)
{
  orientation = orient;
  // With EPD_ROT_INVERTED_PORTRAIT set at init time, epdiy exposes a
  // logical portrait space of 540x960 (EPD_HEIGHT x EPD_WIDTH). Keep the
  // logical Screen dimensions fixed to that space regardless of the
  // orientation enum so the layout engine can use the full page.
  width  = EPD_HEIGHT;  // 540
  height = EPD_WIDTH;   // 960
}

static inline uint8_t map_gray(uint8_t v)
{
  // Assume incoming bitmaps/glyphs are 0 (black) .. 255 (white).
  // epdiy expects 0 = black, 255 = white as well, but many callers
  // in EPub_InkPlate use inverted grayscale, so start with a simple
  // inversion that we can tweak based on visual tests.
  return 255 - v;
}

void Screen::draw_bitmap(const unsigned char * bitmap_data, Dim dim, Pos pos)
{
  if (!s_epd_initialized || (bitmap_data == nullptr)) return;

  uint16_t x_max = pos.x + dim.width;
  uint16_t y_max = pos.y + dim.height;

  if (x_max > width)  x_max = width;
  if (y_max > height) y_max = height;

  for (uint16_t y = pos.y, q = 0; y < y_max; ++y, ++q) {
    for (uint16_t x = pos.x, p = q * dim.width; x < x_max; ++x, ++p) {
      uint8_t v = bitmap_data[p];
      uint8_t c = map_gray(v);
      epd_draw_pixel(x, y, c, s_framebuffer);
    }
  }
}

void Screen::draw_glyph(const unsigned char * bitmap_data, Dim dim, Pos pos, uint16_t pitch)
{
  if (!s_epd_initialized || (bitmap_data == nullptr)) return;

  uint16_t x_max = pos.x + dim.width;
  uint16_t y_max = pos.y + dim.height;

  if (x_max > width)  x_max = width;
  if (y_max > height) y_max = height;

  for (uint16_t j = 0; j < dim.height && (pos.y + j) < y_max; ++j) {
    const uint8_t *row = bitmap_data + j * pitch;
    for (uint16_t i = 0; i < dim.width && (pos.x + i) < x_max; ++i) {
      uint8_t v = row[i];
      if (!v) continue; // background
      uint8_t c = map_gray(v);
      epd_draw_pixel(pos.x + i, pos.y + j, c, s_framebuffer);
    }
  }
}

void Screen::draw_rectangle(Dim dim, Pos pos, uint8_t color)
{
  if (!s_epd_initialized) return;

  EpdRect r {
    .x = (int)pos.x,
    .y = (int)pos.y,
    .width  = (int)dim.width,
    .height = (int)dim.height,
  };
  uint8_t c = map_gray(color);
  epd_draw_rect(r, c, s_framebuffer);
}

void Screen::draw_round_rectangle(Dim dim, Pos pos, uint8_t color)
{
  // Approximate with a simple rectangle for now.
  draw_rectangle(dim, pos, color);
}

void Screen::colorize_region(Dim dim, Pos pos, uint8_t color)
{
  if (!s_epd_initialized) return;

  EpdRect r {
    .x = (int)pos.x,
    .y = (int)pos.y,
    .width  = (int)dim.width,
    .height = (int)dim.height,
  };
  uint8_t c = map_gray(color);
  epd_fill_rect(r, c, s_framebuffer);
}

#endif // BOARD_TYPE_PAPER_S3
