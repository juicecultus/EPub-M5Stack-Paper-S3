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
  // Convert an 8-bit grayscale value (0=black..255=white) into an epdiy
  // API color byte (upper nibble significant).
  return (uint8_t)(v & 0xF0);
}

static inline uint8_t gray8_to_nibble(uint8_t v)
{
  // 0 (black) .. 255 (white) => 0..15
  return (uint8_t)(v >> 4);
}

static inline uint8_t alpha8_to_nibble(uint8_t a)
{
  // Alpha 0 (transparent) .. 255 (opaque) => 15..0 (white..black)
  return (uint8_t)(15 - (a >> 4));
}

static inline uint8_t gray3_to_nibble(uint8_t v)
{
  // 3-bit grayscale 0..7 => 0..15
  return (uint8_t)((v * 15 + 3) / 7);
}

static inline void set_pixel_nibble_physical(uint16_t x, uint16_t y, uint8_t nibble)
{
  // Write a 4-bpp pixel directly into the epdiy framebuffer.
  // x: 0..EPD_WIDTH-1 (960), y: 0..EPD_HEIGHT-1 (540)
  uint8_t * buf_ptr = &s_framebuffer[y * (EPD_WIDTH / 2) + (x >> 1)];
  if (x & 1) {
    *buf_ptr = (uint8_t)((*buf_ptr & 0x0F) | ((nibble & 0x0F) << 4));
  } else {
    *buf_ptr = (uint8_t)((*buf_ptr & 0xF0) | (nibble & 0x0F));
  }
}

static inline void set_pixel_nibble_screen(uint16_t x, uint16_t y, uint8_t nibble)
{
  // Screen coordinates for Paper S3 are logical portrait (width=540, height=960)
  // with epdiy set to EPD_ROT_INVERTED_PORTRAIT.
  // The equivalent physical coordinates in the 960x540 framebuffer are:
  //   x_phys = y
  //   y_phys = (EPD_HEIGHT - 1) - x
  set_pixel_nibble_physical(y, (uint16_t)((EPD_HEIGHT - 1) - x), nibble);
}

void Screen::draw_bitmap(const unsigned char * bitmap_data, Dim dim, Pos pos)
{
  if (!s_epd_initialized || (bitmap_data == nullptr)) return;

  uint16_t x_max = pos.x + dim.width;
  uint16_t y_max = pos.y + dim.height;

  if (x_max > width)  x_max = width;
  if (y_max > height) y_max = height;

  // For best locality with the rotated coordinate system, iterate X (screen) outer.
  for (uint16_t x = pos.x; x < x_max; ++x) {
    for (uint16_t y = pos.y; y < y_max; ++y) {
      const uint32_t p = (uint32_t)(y - pos.y) * dim.width + (x - pos.x);
      const uint8_t v = bitmap_data[p];
      set_pixel_nibble_screen(x, y, gray8_to_nibble(v));
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

  // Glyph buffer is 8-bit alpha (0=transparent..255=opaque). We draw it as
  // black with intensity proportional to alpha.
  for (uint16_t i = 0; i < dim.width && (pos.x + i) < x_max; ++i) {
    const uint16_t x = (uint16_t)(pos.x + i);
    for (uint16_t j = 0; j < dim.height && (pos.y + j) < y_max; ++j) {
      const uint16_t y = (uint16_t)(pos.y + j);
      const uint8_t a = bitmap_data[j * pitch + i];
      if (!a) continue;
      const uint8_t nib = alpha8_to_nibble(a);
      if (nib == 0x0F) continue;
      set_pixel_nibble_screen(x, y, nib);
    }
  }
}

void Screen::draw_rectangle(Dim dim, Pos pos, uint8_t color)
{
  if (!s_epd_initialized) return;

  uint16_t x_max = pos.x + dim.width;
  uint16_t y_max = pos.y + dim.height;
  if (x_max > width)  x_max = width;
  if (y_max > height) y_max = height;
  if (x_max <= pos.x || y_max <= pos.y) return;

  const uint8_t nib = gray3_to_nibble(color);

  // Top and bottom edges
  for (uint16_t x = pos.x; x < x_max; ++x) {
    set_pixel_nibble_screen(x, pos.y, nib);
    set_pixel_nibble_screen(x, (uint16_t)(y_max - 1), nib);
  }
  // Left and right edges
  for (uint16_t y = pos.y; y < y_max; ++y) {
    set_pixel_nibble_screen(pos.x, y, nib);
    set_pixel_nibble_screen((uint16_t)(x_max - 1), y, nib);
  }
}

void Screen::draw_round_rectangle(Dim dim, Pos pos, uint8_t color)
{
  // Approximate with a simple rectangle for now.
  draw_rectangle(dim, pos, color);
}

void Screen::colorize_region(Dim dim, Pos pos, uint8_t color)
{
  if (!s_epd_initialized) return;

  uint16_t x_max = pos.x + dim.width;
  uint16_t y_max = pos.y + dim.height;
  if (x_max > width)  x_max = width;
  if (y_max > height) y_max = height;
  if (x_max <= pos.x || y_max <= pos.y) return;

  const uint8_t nib = gray3_to_nibble(color);

  for (uint16_t x = pos.x; x < x_max; ++x) {
    for (uint16_t y = pos.y; y < y_max; ++y) {
      set_pixel_nibble_screen(x, y, nib);
    }
  }
}

#endif // BOARD_TYPE_PAPER_S3
