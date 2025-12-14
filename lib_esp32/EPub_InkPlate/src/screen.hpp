// Copyright (c) 2020 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.

#pragma once
#include "global.hpp"

#include "non_copyable.hpp"
#include "inkplate_platform.hpp"

/**
 * @brief Low level logical Screen display
 * 
 * This class implements the low level methods required to paint
 * on the display. Under the InkPlate6, it is using the EInk display driver. 
 * For BOARD_TYPE_PAPER_S3, a minimal stub implementation is provided
 * that will later be backed by the epdiy renderer.
 */

#if defined(BOARD_TYPE_PAPER_S3)

class Screen : NonCopyable
{
  public:
    static constexpr uint8_t    BLACK_COLOR =   0;
    static constexpr uint8_t    WHITE_COLOR =   7;
    static constexpr int8_t     IDENT       =   4;
    static constexpr uint16_t   RESOLUTION  = 212;  ///< Approximate pixels per inch

    enum class Orientation     : int8_t { LEFT, RIGHT, BOTTOM, TOP };
    enum class PixelResolution : int8_t { ONE_BIT, THREE_BITS };

    void          draw_bitmap(const unsigned char * bitmap_data, Dim dim, Pos pos);
    void           draw_glyph(const unsigned char * bitmap_data, Dim dim, Pos pos, uint16_t pitch);
    void       draw_rectangle(Dim dim, Pos pos, uint8_t color);
    void draw_round_rectangle(Dim dim, Pos pos, uint8_t color);
    void      colorize_region(Dim dim, Pos pos, uint8_t color);

    void clear();
    void update(bool no_full = false);

  private:
    static constexpr char const * TAG = "Screen";
    
    static uint16_t width;
    static uint16_t height;

    static Screen singleton;
    Screen() : pixel_resolution(PixelResolution::ONE_BIT),
               orientation(Orientation::BOTTOM) { };

    PixelResolution   pixel_resolution;
    Orientation       orientation;

  public:
    static Screen & get_singleton() noexcept { return singleton; }
    void setup(PixelResolution resolution, Orientation orientation);
    void set_pixel_resolution(PixelResolution resolution, bool force = false);
    void set_orientation(Orientation orient);
    inline Orientation get_orientation() { return orientation; }
    inline PixelResolution get_pixel_resolution() { return pixel_resolution; }
    void force_full_update();

    inline static uint16_t get_width() { return width; }
    inline static uint16_t get_height() { return height; }
};

#else  // !BOARD_TYPE_PAPER_S3

class Screen : NonCopyable
{
  public:
    static constexpr uint8_t    BLACK_COLOR           =   0;
    static constexpr uint8_t    WHITE_COLOR           =   7;
    #if INKPLATE_10
      static constexpr int8_t   IDENT                 =   2;
      static constexpr int8_t   PARTIAL_COUNT_ALLOWED =  10;
      static constexpr uint16_t RESOLUTION            = 150;  ///< Pixels per inch
    #elif INKPLATE_6
      static constexpr int8_t   IDENT                 =   1;
      static constexpr int8_t   PARTIAL_COUNT_ALLOWED =  10;
      static constexpr uint16_t RESOLUTION            = 166;  ///< Pixels per inch
    #elif INKPLATE_6PLUS
      static constexpr int8_t   IDENT                 =   3;
      static constexpr int16_t  PARTIAL_COUNT_ALLOWED =  10;
      static constexpr uint16_t RESOLUTION            = 212;  ///< Pixels per inch
    #endif
    enum class Orientation     : int8_t { LEFT, RIGHT, BOTTOM, TOP };
    enum class PixelResolution : int8_t { ONE_BIT, THREE_BITS };

    void          draw_bitmap(const unsigned char * bitmap_data, Dim dim, Pos pos);
    void           draw_glyph(const unsigned char * bitmap_data, Dim dim, Pos pos, uint16_t pitch);
    void       draw_rectangle(Dim dim, Pos pos, uint8_t color);
    void draw_round_rectangle(Dim dim, Pos pos, uint8_t color);
    void      colorize_region(Dim dim, Pos pos, uint8_t color);

    void low_colorize_1bit(Dim dim, Pos pos, uint8_t color);
    void low_colorize_3bit(Dim dim, Pos pos, uint8_t color);

    inline void clear()  {
      if (pixel_resolution == PixelResolution::ONE_BIT) { 
        frame_buffer_1bit->clear();
      }
      else {
        frame_buffer_3bit->clear();
      } 
    }
    
    inline void update(bool no_full = false) { 
      if (pixel_resolution == PixelResolution::ONE_BIT) {
        if (no_full) {
          e_ink.partial_update(*frame_buffer_1bit);
          partial_count = 0;
        }
        else {
          if (partial_count <= 0) {
            //e_ink.clean();
            e_ink.update(*frame_buffer_1bit);
            partial_count = PARTIAL_COUNT_ALLOWED;
          }
          else {
            e_ink.partial_update(*frame_buffer_1bit);
            partial_count--;
          }
        }
      }
      else {
        e_ink.update(*frame_buffer_3bit);
      }
    }

  private:
    static constexpr char const * TAG = "Screen";
    static const uint8_t          LUT1BIT[8];
    static const uint8_t          LUT1BIT_INV[8];
    
    static uint16_t width;
    static uint16_t height;

    static Screen singleton;
    Screen() : partial_count(0), 
               frame_buffer_1bit(nullptr), 
               frame_buffer_3bit(nullptr) { };

    int16_t           partial_count;
    FrameBuffer1Bit * frame_buffer_1bit;
    FrameBuffer3Bit * frame_buffer_3bit;
    PixelResolution   pixel_resolution;
    Orientation       orientation;

    enum class Corner : uint8_t { TOP_LEFT, TOP_RIGHT, LOWER_LEFT, LOWER_RIGHT };
    void draw_arc(uint16_t x_mid,  uint16_t y_mid,  uint8_t radius, Corner corner, uint8_t color);

  public:
    static Screen & get_singleton() noexcept { return singleton; }
    void setup(PixelResolution resolution, Orientation orientation);
    void set_pixel_resolution(PixelResolution resolution, bool force = false);
    void set_orientation(Orientation orient);
    inline Orientation get_orientation() { return orientation; }
    inline PixelResolution get_pixel_resolution() { return pixel_resolution; }
    inline void force_full_update() { partial_count = 0; }

    #if INKPLATE_6PLUS
      void to_user_coord(uint16_t & x, uint16_t & y);
    #endif

    inline static uint16_t get_width() { return width; }
    inline static uint16_t get_height() { return height; }
};

#endif // BOARD_TYPE_PAPER_S3

#if __SCREEN__
  Screen & screen = Screen::get_singleton();
#else
  extern Screen & screen;
#endif
