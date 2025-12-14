// Copyright (c) 2020 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.

#include "models/png_image.hpp"
#include "helpers/unzip.hpp"
#include "viewers/msg_viewer.hpp"
#include "alloc.hpp"

#if defined(BOARD_TYPE_PAPER_S3)
  #include <PNGdec.h>
#else
  #include "mypngle.hpp"
#endif

#include <cmath>

static uint32_t  load_start_time;
static bool      waiting_msg_shown;
static uint16_t  pix_count;

#if defined(BOARD_TYPE_PAPER_S3)

struct PngDecCtx {
  PNG * png;
  Image::ImageData * image_data;
  uint16_t src_w;
  uint16_t src_h;
  uint16_t dst_w;
  uint16_t dst_h;
  int8_t scale;
  uint16_t * rgb565_line;
};

static inline uint8_t rgb565_to_gray8(uint16_t c)
{
  // RGB565 -> 8-bit luma-ish
  uint8_t r5 = (c >> 11) & 0x1f;
  uint8_t g6 = (c >> 5) & 0x3f;
  uint8_t b5 = c & 0x1f;
  uint8_t r8 = (r5 << 3) | (r5 >> 2);
  uint8_t g8 = (g6 << 2) | (g6 >> 4);
  uint8_t b8 = (b5 << 3) | (b5 >> 2);
  uint16_t y = (uint16_t)(r8 * 30 + g8 * 59 + b8 * 11);
  return (uint8_t)(y / 100);
}

static int PNGDraw(PNGDRAW *pDraw)
{
  static constexpr char const * TAG = "PngImagePNGDraw";

  PngDecCtx * ctx = (PngDecCtx *)pDraw->pUser;
  if (ctx == nullptr || ctx->png == nullptr || ctx->image_data == nullptr || ctx->image_data->bitmap == nullptr) {
    return 0;
  }

  if (ctx->rgb565_line == nullptr) {
    return 0;
  }

  // Convert this source line to RGB565 with a white background (handles alpha).
  ctx->png->getLineAsRGB565(pDraw, ctx->rgb565_line, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);

  const uint16_t src_y = (uint16_t)pDraw->y;
  const uint16_t dst_y = (ctx->scale > 0) ? (src_y >> ctx->scale) : src_y;

  if (dst_y >= ctx->dst_h) {
    return 1;
  }

  const uint16_t step = (ctx->scale > 0) ? (uint16_t)(1U << ctx->scale) : 1U;
  if (step > 1 && (src_y % step) != 0) {
    return 1;
  }

  uint8_t * dst_row = ctx->image_data->bitmap + (dst_y * ctx->dst_w);
  for (uint16_t src_x = 0; src_x < (uint16_t)pDraw->iWidth; src_x++) {
    if (step > 1 && (src_x % step) != 0) continue;
    const uint16_t dst_x = (ctx->scale > 0) ? (src_x >> ctx->scale) : src_x;
    if (dst_x >= ctx->dst_w) break;
    dst_row[dst_x] = rgb565_to_gray8(ctx->rgb565_line[src_x]);
  }

  return 1;
}

#else

static int32_t 
get_int_big_endian(uint8_t * a) {
  return
    a[0] << 24 |
    a[1] << 16 |
    a[2] <<  8 |
    a[3];
}

static void 
on_draw(pngle_t *pngle, uint32_t x, uint32_t y, uint8_t pix, uint8_t alpha)
{
  static constexpr char const * TAG = "PngImageOnDraw";

  #if EPUB_INKPLATE_BUILD
  // if (!waiting_msg_shown && (--pix_count == 0)) {
  //   pix_count = 2048;

  //   if ((ESP::millis() - load_start_time) > 2000) {
  //     waiting_msg_shown = true;

  //     msg_viewer.show(
  //       MsgViewer::MsgType::INFO, 
  //       false, false, 
  //       "Retrieving Image", 
  //       "The application is retrieving image(s) from the e-book. Please wait."
  //     );
  //   }
  // }
  #endif

  const Image::ImageData * data = ((PngImage *) mypngle_get_user_data(pngle))->get_image_data();
  int8_t   scale = ((PngImage *) mypngle_get_user_data(pngle))->get_scale_factor();
  uint16_t trans = alpha; // 0: fully transparent, 255: fully opaque

  if (scale) {
    x >>= scale;
    y >>= scale;
  }

  if ((x >= data->dim.width) || (y >= data->dim.height)) {
    LOG_D("Rect outside of image dimensions!");
    return;
  }
  else {
    data->bitmap[(y * data->dim.width) + x] = (trans * pix / 255) + (255 - trans);
  }
}

#endif

PngImage::PngImage(std::string filename, Dim max, bool load_bitmap) : Image(filename)
{
  LOG_I("Loading PNG image file %s", filename.c_str());

  #if defined(BOARD_TYPE_PAPER_S3)
    uint32_t png_size = 0;
    char * png_data = unzip.get_file(filename.c_str(), png_size);
    if (png_data == nullptr || png_size == 0) {
      LOG_E("Unable to load PNG from EPUB: %s", filename.c_str());
      return;
    }

    PNG png;
    int rc = png.openRAM((uint8_t *)png_data, (int)png_size, PNGDraw);
    if (rc != PNG_SUCCESS) {
      LOG_E("PNGdec open failed. Error: %d", png.getLastError());
      free(png_data);
      return;
    }

    const uint16_t orig_w = (uint16_t)png.getWidth();
    const uint16_t orig_h = (uint16_t)png.getHeight();
    orig_dim = Dim(orig_w, orig_h);
    size_retrieved = true;

    scale = 0;
    while (scale < 3 && ((orig_w >> scale) > max.width || (orig_h >> scale) > max.height)) {
      scale++;
    }

    const uint16_t out_w = (uint16_t)(orig_w >> scale);
    const uint16_t out_h = (uint16_t)(orig_h >> scale);

    LOG_D("Image size: [%d, %d] %d bytes.", out_w, out_h, out_w * out_h);

    if (!load_bitmap) {
      image_data.dim = Dim(out_w, out_h);
      png.close();
      free(png_data);
      return;
    }

    image_data.dim = Dim(out_w, out_h);
    image_data.bitmap = (uint8_t *) allocate(out_w * out_h);
    if (image_data.bitmap == nullptr) {
      png.close();
      free(png_data);
      return;
    }

    uint16_t * rgb565_line = (uint16_t *) allocate(orig_w * sizeof(uint16_t));
    if (rgb565_line == nullptr) {
      png.close();
      free(png_data);
      return;
    }

    #if EPUB_INKPLATE_BUILD
      load_start_time   = ESP::millis();
      waiting_msg_shown = false;
      pix_count         = 2048;
    #endif

    PngDecCtx ctx;
    ctx.png = &png;
    ctx.image_data = &image_data;
    ctx.src_w = orig_w;
    ctx.src_h = orig_h;
    ctx.dst_w = out_w;
    ctx.dst_h = out_h;
    ctx.scale = scale;
    ctx.rgb565_line = rgb565_line;

    rc = png.decode(&ctx, PNG_FAST_PALETTE);
    if (rc != PNG_SUCCESS) {
      LOG_E("PNGdec decode failed. Error: %d", png.getLastError());
    }

    free(rgb565_line);
    png.close();
    free(png_data);

    LOG_I("PNG Image load complete");

  #else
    if (unzip.open_stream_file(filename.c_str(), file_size)) {

      pngle_t * pngle   = mypngle_new();
      size_t    sz_work = WORK_SIZE;
      uint8_t * work    = (uint8_t *) allocate(sz_work);
      bool      first   = true;
      int32_t   total   = 0;

      mypngle_set_user_data(pngle, this);

      mypngle_set_draw_callback(pngle, on_draw);

      #if EPUB_INKPLATE_BUILD
        load_start_time   = ESP::millis();
        waiting_msg_shown = false;
        pix_count         = 2048;
      #endif

      /* Prepare to decompress */

      uint32_t size = WORK_SIZE;
      while (unzip.get_stream_data((char *) work, size)) {
        if (size == 0) break;

        if (first) {
          first = false;

          if (size < 24) break;
          uint32_t width  = get_int_big_endian(&work[16]);
          uint32_t height = get_int_big_endian(&work[20]);

          uint32_t h = height;
          uint32_t w;
          
          scale      = 0;
          float s    = 1.0;

          if (width > max.width) {
            s = ((float) max.width) / width;
            h = floor(s * height) + 1;
          }
          if (h > max.height) {
            s = ((float) max.height) / height;
          }

          if (s < 1.0) {
            // if (s <= 0.125) scale = 3;
            // else if (s <=  0.25) scale = 2;
            // else if (s <=   0.5) scale = 1;
            if      (s < 0.25) scale = 3;
            else if (s <  0.5) scale = 2;
            else scale = 1;
          
            h = height >> scale;
            w = width  >> scale;
          }
          else {
            h = height;
            w = width;
          }

          LOG_D("Image size: [%d, %d] %d bytes.", w, h, w * h);

          if (load_bitmap) {
            if ((image_data.bitmap = (uint8_t *) allocate(w * h)) == nullptr) break;
            image_data.dim = Dim(w, h);
          }
          else {
            image_data.dim = Dim(w, h);
            break;
          }
        }

        int32_t res = mypngle_feed(pngle, work, size);

        if (res < 0) {
          LOG_E("Unable to load image. Error msg: %s", mypngle_error(pngle));
          break;
        }

        total += size;
        if (total >= file_size) break;

        size = WORK_SIZE;
      }

      free(work);
      mypngle_destroy(pngle);
      unzip.close_stream_file();

      LOG_I("PNG Image load complete");
    }
  #endif
}
