// Copyright (c) 2020 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.

#include "models/jpeg_image.hpp"

#include "helpers/unzip.hpp"
#include "viewers/msg_viewer.hpp"

#include "alloc.hpp"

#if defined(BOARD_TYPE_PAPER_S3)
  #include <JPEGDEC.h>
#else
  #include "tjpgdec.hpp"
#endif

static uint32_t  load_start_time;
static bool      waiting_msg_shown;

// static bool first = false;

#if defined(BOARD_TYPE_PAPER_S3)

struct JpegDecCtx {
  Image::ImageData * image_data;
};

static int JPEGDraw(JPEGDRAW *pDraw)
{
  static constexpr char const * TAG = "JPegImageJPEGDraw";

  JpegDecCtx * ctx = (JpegDecCtx *)pDraw->pUser;
  if (ctx == nullptr || ctx->image_data == nullptr || ctx->image_data->bitmap == nullptr) {
    return 0;
  }

  #if EPUB_INKPLATE_BUILD
    if (!waiting_msg_shown && ((ESP::millis() - load_start_time) > 2000)) {
      waiting_msg_shown = true;

      msg_viewer.show(
        MsgViewer::MsgType::INFO,
        false, false,
        "Retrieving Image",
        "The application is retrieving image(s) from the e-book file. Please wait."
      );
    }
  #endif

  // For EIGHT_BIT_GRAYSCALE, the library provides 1 byte per pixel, but the pointer type is uint16_t*.
  const uint8_t * src = (const uint8_t *)pDraw->pPixels;
  if (src == nullptr) {
    return 0;
  }

  const uint16_t out_w = ctx->image_data->dim.width;
  const uint16_t out_h = ctx->image_data->dim.height;

  if ((pDraw->x < 0) || (pDraw->y < 0)) {
    return 0;
  }
  if ((uint32_t)pDraw->x >= out_w || (uint32_t)pDraw->y >= out_h) {
    return 1;
  }

  const int copy_w = (pDraw->iWidthUsed > 0) ? pDraw->iWidthUsed : pDraw->iWidth;
  const int max_w = (int)out_w - pDraw->x;
  const int w = (copy_w < max_w) ? copy_w : max_w;

  for (int yy = 0; yy < pDraw->iHeight; yy++) {
    const int dst_y = pDraw->y + yy;
    if (dst_y >= (int)out_h) break;
    uint8_t * dst = ctx->image_data->bitmap + (dst_y * out_w + pDraw->x);
    memcpy(dst, src + (yy * pDraw->iWidth), w);
  }

  return 1;
}

#endif // BOARD_TYPE_PAPER_S3

#if !defined(BOARD_TYPE_PAPER_S3)

static size_t in_func (     /* Returns number of bytes read (zero on error) */
    JDEC    * jd,    /* Decompression object */
    uint8_t * buff,  /* Pointer to the read buffer (null to remove data) */
    size_t    nbyte  /* Number of bytes to read/remove */
)
{
  if (buff) { /* Read data from imput stream */
    uint32_t size = nbyte;
    size_t res = unzip.get_stream_data((char *) buff, size) ? size : 0;
    // if (first) {
    //   first = false;
    //   std::cout << "----- Unzip content -----" << std::endl;
    //   std::cout << "Size read: " << size << std::endl;
    //   for (int i = 0; i < 200; i++) {
    //     std::cout << std::hex << std::setw(2) << +buff[i];
    //   }
    //   std::cout << std::endl << "-----" << std::endl;    
    // }
    return res;
  } else {    /* Remove data from input stream */
    return unzip.stream_skip(nbyte) ? nbyte : 0;
  }
}

/*------------------------------*/
/* User defined output funciton */
/*------------------------------*/

static int out_func (       /* Returns 1 to continue, 0 to abort */
    JDEC  * jd,      /* Decompression object */
    void  * bitmap,  /* Bitmap data to be output */
    JRECT * rect     /* Rectangular region of output image */
)
{
  static constexpr char const * TAG = "JPegImageOutFunc";
  
  Image::ImageData * image_data = (Image::ImageData *) jd->device;
  uint8_t * src, * dst;
  uint16_t y, bws, bwd;

  #if EPUB_INKPLATE_BUILD
    if (!waiting_msg_shown && ((ESP::millis() - load_start_time) > 2000)) {
      waiting_msg_shown = true;

      msg_viewer.show(
        MsgViewer::MsgType::INFO, 
        false, false, 
        "Retrieving Image", 
        "The application is retrieving image(s) from the e-book file. Please wait."
      );
    }
  #endif

  if ((rect->right >= image_data->dim.width) || (rect->bottom >= image_data->dim.height)) {
    LOG_E("Rect outside of image dimensions!");
    return 0;
  }
  /* Copy the output image rectangle to the frame buffer (assuming BW output) */
  src = (uint8_t *) bitmap;
  if (src == nullptr) return 0;

  dst = image_data->bitmap + (rect->top * image_data->dim.width + rect->left);  /* Left-top of destination rectangular */
  bws = (rect->right - rect->left + 1);     /* Width of output rectangular [byte] */
  bwd = image_data->dim.width;              /* Width of frame buffer [byte] */
  for (y = rect->top; y <= rect->bottom; y++) {
    memcpy(dst, src, bws);   /* Copy a line */
    src += bws; dst += bwd;  /* Next line */
  }

  return 1;    /* Continue to decompress */
}

#endif // !BOARD_TYPE_PAPER_S3

JPegImage::JPegImage(std::string filename, Dim max, bool load_bitmap) : Image(filename)
{
  LOG_D("Loading image file %s", filename.c_str());

#if defined(BOARD_TYPE_PAPER_S3)
  uint32_t jpg_size = 0;
  char * jpg_data = unzip.get_file(filename.c_str(), jpg_size);
  if (jpg_data == nullptr || jpg_size == 0) {
    LOG_E("Unable to load JPEG from EPUB: %s", filename.c_str());
    return;
  }

  JPEGDEC jpeg;
  if (!jpeg.openRAM((uint8_t *)jpg_data, (int)jpg_size, JPEGDraw)) {
    LOG_E("JPEGDEC open failed. Error: %d", jpeg.getLastError());
    free(jpg_data);
    return;
  }

    const uint16_t orig_w = (uint16_t)jpeg.getWidth();
    const uint16_t orig_h = (uint16_t)jpeg.getHeight();
    orig_dim = Dim(orig_w, orig_h);
    size_retrieved = true;

    uint8_t scale = 0;
    while (scale < 3 && ((orig_w >> scale) > max.width || (orig_h >> scale) > max.height)) {
      scale++;
    }

    const uint16_t out_w = (uint16_t)(orig_w >> scale);
    const uint16_t out_h = (uint16_t)(orig_h >> scale);

    LOG_D("Image size: [%d, %d] %d bytes.", out_w, out_h, out_w * out_h);

    if (load_bitmap) {
      image_data.dim = Dim(out_w, out_h);
      image_data.bitmap = (uint8_t *)allocate(out_w * out_h);
      if (image_data.bitmap == nullptr) {
        jpeg.close();
        free(jpg_data);
        return;
      }
    }
    else {
      image_data.dim = Dim(out_w, out_h);
      jpeg.close();
      free(jpg_data);
      return;
    }

    jpeg.setPixelType(EIGHT_BIT_GRAYSCALE);

    int options = JPEG_LUMA_ONLY;
    if (scale == 1) options |= JPEG_SCALE_HALF;
    else if (scale == 2) options |= JPEG_SCALE_QUARTER;
    else if (scale == 3) options |= JPEG_SCALE_EIGHTH;

    JpegDecCtx ctx{&image_data};
    jpeg.setUserPointer(&ctx);

    #if EPUB_INKPLATE_BUILD
      load_start_time   = ESP::millis();
      waiting_msg_shown = false;
    #endif

    if (!jpeg.decode(0, 0, options)) {
      LOG_E("JPEGDEC decode failed. Error: %d", jpeg.getLastError());
    }

    jpeg.close();
    free(jpg_data);

  #else
    if (unzip.open_stream_file(filename.c_str(), file_size)) {
      JRESULT   res;                /* Result code of TJpgDec API */
      JDEC      jdec;               /* Decompression object */
      uint8_t * work;
      size_t    sz_work = WORK_SIZE;

      /* Prepare to decompress */
      work = (uint8_t *) allocate(sz_work);
      res  = jdec_prepare(&jdec, in_func, work, sz_work, &image_data);
      if (res == JDR_OK) {
        uint8_t scale = 0;
        uint16_t width = jdec.width;
        while (max.width < width) {
          scale += 1;
          width >>= 1;
        }
        uint16_t height = jdec.height >> scale;
        while (max.height < height) {
          scale += 1;
          height >>= 1;
        }
        if (scale > 3) scale = 3;
        width  = jdec.width  >> scale;
        height = jdec.height >> scale;

        LOG_D("Image size: [%d, %d] %d bytes.", width, height, width * height);
        
        if (load_bitmap) {
          if ((image_data.bitmap = (uint8_t *) allocate(width * height)) != nullptr) {
            image_data.dim    = Dim(width, height);
            // first = true;
            #if EPUB_INKPLATE_BUILD
              load_start_time   = ESP::millis();
              waiting_msg_shown = false;
            #endif
            res = jdec_decomp(&jdec, out_func, scale);
          }
        }
        else {
          image_data.dim = Dim(width, height);
        }
      }
      else {
        LOG_E("Unable to load image. Error code: %d", res);
      }

      free(work);
      unzip.close_stream_file();
    }
  #endif
}
