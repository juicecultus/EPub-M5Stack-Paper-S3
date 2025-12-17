// Copyright (c) 2020 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.

#define __COMMON_ACTIONS__ 1
#include "controllers/common_actions.hpp"

#include "controllers/app_controller.hpp"
#include "controllers/books_dir_controller.hpp"
#include "controllers/book_controller.hpp"
#include "controllers/event_mgr.hpp"
#include "viewers/msg_viewer.hpp"
#include "viewers/menu_viewer.hpp"
#include "models/books_dir.hpp"
#include "models/config.hpp"
#include "models/epub.hpp"
#include "models/image_factory.hpp"
#include "viewers/page.hpp"

#if EPUB_INKPLATE_BUILD
  #include "inkplate_platform.hpp"
  #include "esp.hpp"
#endif

#if defined(BOARD_TYPE_PAPER_S3)
  #include "models/nvs_mgr.hpp"
  #include "alloc.hpp"
  #include <dirent.h>
  #include <cstring>
  #include <strings.h>
  #include <esp_system.h>
  #include <esp_random.h>
  #include "stb_image_resize.h"
#endif

void
CommonActions::return_to_last()
{
  app_controller.set_controller(AppController::Ctrl::LAST);
}

void
CommonActions::render_sleep_screen()
{
#if defined(BOARD_TYPE_PAPER_S3) && EPUB_INKPLATE_BUILD
  static constexpr char const * IMAGES_DIR = MAIN_FOLDER "/images";

  auto draw_image_sharp = [](const uint8_t * bitmap, Dim dim) -> bool {
    if (bitmap == nullptr || dim.width == 0 || dim.height == 0) {
      return false;
    }

    const uint16_t screen_w = Screen::get_width();
    const uint16_t screen_h = Screen::get_height();

    uint16_t draw_w = dim.width;
    uint16_t draw_h = dim.height;

    // Never upscale. Only downscale-to-fit.
    if ((draw_w > screen_w) || (draw_h > screen_h)) {
      uint32_t w = screen_w;
      uint32_t h = ((uint32_t)dim.height * w) / (uint32_t)dim.width;
      if (h > screen_h) {
        h = screen_h;
        w = ((uint32_t)dim.width * h) / (uint32_t)dim.height;
      }
      if (w < 1) w = 1;
      if (h < 1) h = 1;
      draw_w = (uint16_t)w;
      draw_h = (uint16_t)h;
    }

    uint8_t * scaled = nullptr;
    const uint8_t * to_draw = bitmap;
    if ((draw_w != dim.width) || (draw_h != dim.height)) {
      scaled = (uint8_t *)allocate((size_t)draw_w * (size_t)draw_h);
      if (scaled == nullptr) {
        return false;
      }

      stbir_resize_uint8_generic(
        bitmap, (int)dim.width, (int)dim.height, 0,
        scaled, (int)draw_w,    (int)draw_h,    0,
        1, -1, 0,
        STBIR_EDGE_CLAMP, STBIR_FILTER_CATMULLROM, STBIR_COLORSPACE_LINEAR,
        nullptr);

      to_draw = scaled;
    }

    const Pos pos(
      (int16_t)((screen_w - draw_w) >> 1),
      (int16_t)((screen_h - draw_h) >> 1));

    screen.force_full_update();
    screen.clear();
    screen.update();
    ESP::delay(50);

    screen.force_full_update();
    screen.clear();
    screen.draw_bitmap(to_draw, Dim(draw_w, draw_h), pos);
    screen.update();

    if (scaled != nullptr) {
      free(scaled);
    }

    return true;
  };

  auto has_img_ext = [](const char * name) -> bool {
    if (name == nullptr) return false;
    const char * dot = strrchr(name, '.');
    if (dot == nullptr) return false;
    if (strcasecmp(dot, ".png") == 0) return true;
    if (strcasecmp(dot, ".jpg") == 0) return true;
    if (strcasecmp(dot, ".jpeg") == 0) return true;
    return false;
  };

  auto render_last_cover_thumbnail = [&]() -> bool {
    uint32_t id = 0;
    NVSMgr::NVSData nvs_data;
    if (!nvs_mgr.get_last(id, nvs_data)) {
      return false;
    }

    #if defined(BOARD_TYPE_PAPER_S3)
      uint8_t * bitmap = nullptr;
      Dim dim(0, 0);
      if (!books_dir.get_full_cover(id, &bitmap, dim) || bitmap == nullptr) {
        return false;
      }
      const bool ok = draw_image_sharp(bitmap, dim);
      free(bitmap);
      return ok;
    #endif

    const int16_t idx = books_dir.get_sorted_idx_from_id(id);
    if (idx < 0) {
      return false;
    }

    const BooksDir::EBookRecord * book = books_dir.get_book_data((uint16_t)idx);
    if (book == nullptr) {
      return false;
    }

    const int16_t src_w = (int16_t)book->cover_width;
    const int16_t src_h = (int16_t)book->cover_height;
    if (src_w <= 0 || src_h <= 0) {
      return false;
    }

    const uint8_t * src = (const uint8_t *)book->cover_bitmap;

    return draw_image_sharp(src, Dim(src_w, src_h));
  };

  auto render_last_cover_high_quality = [&]() -> bool {
    uint32_t id = 0;
    NVSMgr::NVSData nvs_data;
    if (!nvs_mgr.get_last(id, nvs_data)) {
      return false;
    }

    const int16_t idx = books_dir.get_sorted_idx_from_id(id);
    if (idx < 0) {
      return false;
    }

    const BooksDir::EBookRecord * book = books_dir.get_book_data((uint16_t)idx);
    if (book == nullptr) {
      return false;
    }

    std::string book_fname = BOOKS_FOLDER "/";
    book_fname += book->filename;

    if (!epub.open_file(book_fname)) {
      return false;
    }

    std::string cover_fname = epub.get_cover_filename();
    if (cover_fname.empty()) {
      return false;
    }

    const Dim decode_max(
      (uint16_t)(Screen::get_width() * 2),
      (uint16_t)(Screen::get_height() * 2));

    std::string located = epub.filename_locate(cover_fname.c_str());
    Image * img = ImageFactory::create(located, decode_max, true);
    if (img == nullptr || img->get_bitmap() == nullptr) {
      if (img != nullptr) delete img;
      return false;
    }

    const bool ok = draw_image_sharp(img->get_bitmap(), img->get_dim());
    delete img;
    return ok;
  };

  auto render_random_image = [&]() -> bool {
    DIR * dp = opendir(IMAGES_DIR);
    if (dp == nullptr) {
      return false;
    }

    int count = 0;
    while (dirent * de = readdir(dp)) {
      if (de->d_name[0] == '.') continue;
      if (!has_img_ext(de->d_name)) continue;
      count++;
    }
    closedir(dp);

    if (count <= 0) {
      return false;
    }

    const int pick = (int)(esp_random() % (uint32_t)count);

    dp = opendir(IMAGES_DIR);
    if (dp == nullptr) {
      return false;
    }

    int idx = 0;
    std::string chosen;
    while (dirent * de = readdir(dp)) {
      if (de->d_name[0] == '.') continue;
      if (!has_img_ext(de->d_name)) continue;
      if (idx == pick) {
        chosen = std::string(IMAGES_DIR) + "/" + de->d_name;
        break;
      }
      idx++;
    }
    closedir(dp);

    if (chosen.empty()) {
      return false;
    }

    const Dim decode_max(
      (uint16_t)(Screen::get_width() * 2),
      (uint16_t)(Screen::get_height() * 2));

    Image * img = ImageFactory::create(chosen, decode_max, true);
    if (img == nullptr) {
      return false;
    }

    const bool ok = draw_image_sharp(img->get_bitmap(), img->get_dim());
    delete img;
    return ok;
  };

  int8_t mode = 0;
  config.get(Config::Ident::SLEEP_SCREEN, &mode);

  bool ok = false;
  if (mode != 0) {
    ok = render_random_image();
  }
  if (!ok) {
    #if defined(BOARD_TYPE_PAPER_S3)
      ok = render_last_cover_thumbnail();
      if (!ok) {
        ok = render_last_cover_high_quality();
      }
    #else
      ok = render_last_cover_high_quality();
      if (!ok) {
        ok = render_last_cover_thumbnail();
      }
    #endif
  }

  if (!ok) {
    screen.force_full_update();
    screen.clear();
    screen.update();
  }
#endif
}

void
CommonActions::show_last_book()
{
  books_dir_controller.show_last_book();
}

void
CommonActions::refresh_books_dir()
{
  int16_t temp;

  books_dir.refresh(nullptr, temp, true);
  app_controller.set_controller(AppController::Ctrl::DIR);
}

void
CommonActions::power_it_off()
{
  #if INKPLATE_6PLUS
    #define MSG "Please press the WakUp Button to restart the device."
    #define INT_PIN TouchScreen::INTERRUPT_PIN
    #define LEVEL 0
  #else
    #define MSG "Please press a key to restart the device."
    #define LEVEL 1
    #if EXTENDED_CASE
      #define INT_PIN PressKeys::INTERRUPT_PIN
    #else
      #define INT_PIN TouchKeys::INTERRUPT_PIN
    #endif
  #endif

  #if defined(BOARD_TYPE_PAPER_S3)
    #undef INT_PIN
    #define INT_PIN ((gpio_num_t)0)
    #undef LEVEL
    #define LEVEL 0
  #endif

  app_controller.going_to_deep_sleep();
  #if EPUB_INKPLATE_BUILD
    #if defined(BOARD_TYPE_PAPER_S3)
      render_sleep_screen();
      ESP::delay(1000);
      inkplate_platform.deep_sleep(INT_PIN, LEVEL);
    #else
      screen.force_full_update();
      msg_viewer.show(MsgViewer::MsgType::INFO, false, true, "Power OFF",
        "Entering Deep Sleep mode. " MSG);
      ESP::delay(1000);
      inkplate_platform.deep_sleep(INT_PIN, LEVEL);
    #endif
  #else
    extern void exit_app();
    exit_app();
    exit(0);
  #endif
}

void
CommonActions::about()
{
  menu_viewer.clear_highlight();
  msg_viewer.show(
    MsgViewer::MsgType::BOOK, 
    false,
    false,
    "About EPub-InkPlate", 
    "EPub EBook Reader Version %s for the InkPlate e-paper display devices. "
    "This application was made by Guy Turcotte, Quebec, QC, Canada, "
    "with great support from e-Radionica.",
    APP_VERSION);
}
