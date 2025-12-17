// Copyright (c) 2020 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.

#define __BOOK_PARAM_CONTROLLER__ 1
#include "controllers/book_param_controller.hpp"

#include "controllers/app_controller.hpp"
#include "controllers/common_actions.hpp"
#include "controllers/books_dir_controller.hpp"
#include "controllers/book_controller.hpp"
#include "models/books_dir.hpp"
#include "models/epub.hpp"
#include "models/config.hpp"
#include "models/page_locs.hpp"
#include "models/toc.hpp"
#include "viewers/menu_viewer.hpp"
#include "viewers/form_viewer.hpp"
#include "viewers/msg_viewer.hpp"

#if EPUB_INKPLATE_BUILD && !BOARD_TYPE_PAPER_S3
  #include "esp_system.h"
  #include "eink.hpp"
  #include "esp.hpp"
  #include "soc/rtc.h"
#endif

#include <sys/stat.h>

#include <cstdio>

static int8_t show_images;
static int8_t font_size;
static int8_t use_fonts_in_book;
static int8_t font;
static int8_t done_res;

#if EPUB_INKPLATE_BUILD && defined(BOARD_TYPE_PAPER_S3)
  static bool wifi_setup_form_is_shown = false;
  static bool wifi_setup_confirm_is_shown = false;

  static bool stop_web_server_on_key = false;
  static uint8_t return_menu_index_on_key = 0;
  static bool wifi_return_to_wifi_menu_on_key = false;
  static bool wifi_start_web_server_after_setup_on_key = false;

  static char wifi_ssid_buf[32];
  static char wifi_pwd_buf[32];
  static int8_t wifi_done = 1;

  #if INKPLATE_6PLUS || TOUCH_TRIAL
    static constexpr int8_t WIFI_FORM_SIZE = 3;
  #else
    static constexpr int8_t WIFI_FORM_SIZE = 2;
  #endif

  static FormEntry wifi_setup_form_entries[WIFI_FORM_SIZE] = {
    { .caption = "WiFi SSID:",
      .u = { .str = { .value = wifi_ssid_buf, .max_len = (uint16_t)sizeof(wifi_ssid_buf), .password = false } },
      .entry_type = FormEntryType::STRING },
    { .caption = "WiFi Password:",
      .u = { .str = { .value = wifi_pwd_buf,  .max_len = (uint16_t)sizeof(wifi_pwd_buf),  .password = true  } },
      .entry_type = FormEntryType::STRING },
    #if INKPLATE_6PLUS || TOUCH_TRIAL
      { .caption = " DONE ",
        .u = { .ch = { .value = &wifi_done, .choice_count = 0, .choices = nullptr } },
        .entry_type = FormEntryType::DONE }
    #endif
  };

  static bool
  wifi_credentials_present()
  {
    std::string ssid;
    config.get(Config::Ident::SSID, ssid);
    if ((ssid == "NONE") || ssid.empty()) return false;
    return true;
  }

  static void
  wifi_load_buffers_from_config()
  {
    std::string ssid;
    std::string pwd;

    config.get(Config::Ident::SSID, ssid);
    config.get(Config::Ident::PWD,  pwd);

    if (ssid == "NONE") ssid.clear();
    if (pwd  == "NONE") pwd.clear();

    (void)snprintf(wifi_ssid_buf, sizeof(wifi_ssid_buf), "%s", ssid.c_str());
    (void)snprintf(wifi_pwd_buf,  sizeof(wifi_pwd_buf),  "%s", pwd.c_str());
  }

  static void
  wifi_show_setup_form(uint8_t return_menu_index, bool return_to_wifi_menu, bool start_web_server_after_setup)
  {
    return_menu_index_on_key = return_menu_index;
    wifi_return_to_wifi_menu_on_key = return_to_wifi_menu;
    wifi_start_web_server_after_setup_on_key = start_web_server_after_setup;
    wifi_load_buffers_from_config();
    wifi_done = 1;

    form_viewer.show(wifi_setup_form_entries, WIFI_FORM_SIZE, nullptr);
    wifi_setup_form_is_shown = true;
  }
#endif

static int8_t old_font_size;
static int8_t old_show_images;
static int8_t old_use_fonts_in_book;
static int8_t old_font;

static void book_param_restore_menu();
static void book_param_about();

#if INKPLATE_6PLUS || TOUCH_TRIAL
  static constexpr int8_t BOOK_PARAMS_FORM_SIZE = 5;
#else
  static constexpr int8_t BOOK_PARAMS_FORM_SIZE = 4;
#endif
static FormEntry book_params_form_entries[BOOK_PARAMS_FORM_SIZE] = {
  { .caption = "Font Size:",
    .u = { .ch = { .value = &font_size,
                   .choice_count = 4,
                   .choices = FormChoiceField::font_size_choices } },
    .entry_type = FormEntryType::HORIZONTAL },
  { .caption = "Use fonts in book:",
    .u = { .ch = { .value = &use_fonts_in_book,
                   .choice_count = 2,
                   .choices = FormChoiceField::yes_no_choices } },
    .entry_type = FormEntryType::HORIZONTAL },
  { .caption = "Font:",
    .u = { .ch = { .value = &font,
                   .choice_count = 8,
                   .choices = FormChoiceField::font_choices } },
    .entry_type = FormEntryType::VERTICAL },
  { .caption = "Show Images in book:",
    .u = { .ch = { .value = &show_images,
                   .choice_count = 2,
                   .choices = FormChoiceField::yes_no_choices } },
    .entry_type = FormEntryType::HORIZONTAL },
  #if INKPLATE_6PLUS || TOUCH_TRIAL
    { .caption = " DONE ",
      .u = { .ch = { .value = &done_res,
                     .choice_count = 0,
                     .choices = nullptr } },
      .entry_type = FormEntryType::DONE }
  #endif
};

static void
book_parameters()
{
  BookParams * book_params = epub.get_book_params();

  book_params->get(BookParams::Ident::SHOW_IMAGES,        &show_images      );
  book_params->get(BookParams::Ident::FONT_SIZE,          &font_size        );
  book_params->get(BookParams::Ident::USE_FONTS_IN_BOOK,  &use_fonts_in_book);
  book_params->get(BookParams::Ident::FONT,               &font             );
  
  if (show_images       == -1) config.get(Config::Ident::SHOW_IMAGES,        &show_images      );
  if (font_size         == -1) config.get(Config::Ident::FONT_SIZE,          &font_size        );
  if (use_fonts_in_book == -1) config.get(Config::Ident::USE_FONTS_IN_BOOKS, &use_fonts_in_book);
  if (font              == -1) config.get(Config::Ident::DEFAULT_FONT,       &font             );
  
  old_show_images        = show_images;
  old_use_fonts_in_book  = use_fonts_in_book;
  old_font               = font;
  old_font_size          = font_size;
  done_res               = 1;

  form_viewer.show(
    book_params_form_entries, 
    BOOK_PARAMS_FORM_SIZE, 
    #if defined(BOARD_TYPE_PAPER_S3)
      nullptr);
    #else
      "(Any item change will trigger book refresh)");
    #endif

  book_param_controller.set_book_params_form_is_shown();
}

static void
revert_to_defaults()
{
  page_locs.stop_document();
  
  EPub::BookFormatParams * book_format_params = epub.get_book_format_params();

  BookParams * book_params = epub.get_book_params();

  old_use_fonts_in_book = book_format_params->use_fonts_in_book;
  old_font              = book_format_params->font;

  constexpr int8_t default_value = -1;

  book_params->put(BookParams::Ident::SHOW_IMAGES,       default_value);
  book_params->put(BookParams::Ident::FONT_SIZE,         default_value);
  book_params->put(BookParams::Ident::FONT,              default_value);
  book_params->put(BookParams::Ident::USE_FONTS_IN_BOOK, default_value);
  
  epub.update_book_format_params();

  book_params->save();

  msg_viewer.show(MsgViewer::MsgType::INFO, 
                  false, false, 
                  "E-book parameters reverted", 
                  "E-book parameters reverted to default values.");

  #if defined(BOARD_TYPE_PAPER_S3)
    msg_viewer.auto_dismiss_in(7000, book_param_restore_menu);
  #endif

  if (old_use_fonts_in_book != book_format_params->use_fonts_in_book) {
    if (book_format_params->use_fonts_in_book) {
      epub.load_fonts();
    }
    else {
      fonts.clear();
      fonts.clear_glyph_caches();
    }
  }

  if (old_font != book_format_params->font) {
    fonts.adjust_default_font(book_format_params->font);
  }
}

static void 
books_list()
{
  app_controller.set_controller(AppController::Ctrl::DIR);
}

static void
delete_book()
{
  msg_viewer.show(MsgViewer::MsgType::CONFIRM, true, false,
                  "Delete e-book", 
                  "The e-book \"%s\" will be deleted. Are you sure?", 
                  epub.get_title());
  book_param_controller.set_delete_current_book();
}

static void 
toc_ctrl()
{
  app_controller.set_controller(AppController::Ctrl::TOC);
}

extern bool start_web_server();
extern void stop_web_server();
extern bool is_web_server_running();

#if EPUB_INKPLATE_BUILD && defined(BOARD_TYPE_PAPER_S3)
  static void wifi_menu_show();
  static void restore_wifi_menu();
  static void wifi_menu_toggle_web_server();
  static void wifi_return_to_menu(uint8_t return_menu_index);
#endif

static void
wifi_mode()
{
  #if EPUB_INKPLATE_BUILD
    #if defined(BOARD_TYPE_PAPER_S3)
      wifi_menu_show();
      return;
    #endif

    epub.close_file();
    fonts.clear(true);
    fonts.clear_glyph_caches();
    
    event_mgr.set_stay_on(true); // DO NOT sleep

    stop_web_server_on_key = start_web_server();
    return_menu_index_on_key = 6;
    book_param_controller.set_wait_for_key_after_wifi();
  #endif
}

static void
power_off()
{
  books_dir_controller.save_last_book(book_controller.get_current_page_id(), true); 
  
  CommonActions::power_it_off();
}

// IMPORTANT!!
// The first (menu[0]) and the last menu entry (the one before END_MENU) MUST ALWAYS BE VISIBLE!!!

static MenuViewer::MenuEntry menu[10] = {
  { MenuViewer::Icon::RETURN,      "Return to the e-books reader",         CommonActions::return_to_last, true , true },
  { MenuViewer::Icon::TOC,         "Table of Content",                     toc_ctrl                     , false, true },
  { MenuViewer::Icon::BOOK_LIST,   "E-Books list",                         books_list                   , true , true },
  { MenuViewer::Icon::FONT_PARAMS, "Current e-book parameters",            book_parameters              , true , true },
  { MenuViewer::Icon::REVERT,      "Revert e-book parameters to "
                                   "default values",                       revert_to_defaults           , true , true },  
  { MenuViewer::Icon::DELETE,      "Delete the current e-book",            delete_book                  , true , true },
  { MenuViewer::Icon::WIFI,        "WiFi Access to the e-books folder",    wifi_mode                    , true , true },
  { MenuViewer::Icon::INFO,        "About the EPub-InkPlate application",  book_param_about             , true , true },
  { MenuViewer::Icon::POWEROFF,    "Power OFF (Deep Sleep)",               power_off                    , true , true },
  { MenuViewer::Icon::END_MENU,    nullptr,                                nullptr                      , false, true }
}; 

#if EPUB_INKPLATE_BUILD && defined(BOARD_TYPE_PAPER_S3)
  static void wifi_menu_back();
  static void wifi_menu_edit_credentials();
  static void wifi_menu_toggle_web_server();

  static MenuViewer::MenuEntry wifi_menu[] = {
    { MenuViewer::Icon::RETURN, "Back",                   wifi_menu_back,             true,  true },
    { MenuViewer::Icon::MAIN_PARAMS,   "WiFi settings",   wifi_menu_edit_credentials,  true,  true },
    { MenuViewer::Icon::WIFI,          "Web server",      wifi_menu_toggle_web_server, true,  true },
    { MenuViewer::Icon::END_MENU, nullptr,                 nullptr,                   false, false }
  };

  static void
  wifi_menu_show()
  {
    page_locs.abort_threads();
    wifi_menu[2].caption = is_web_server_running() ? "Stop web server" : "Start web server";
    menu_viewer.show(wifi_menu, 0, false);
  }

  static void
  wifi_return_to_menu(uint8_t return_menu_index)
  {
    if (wifi_return_to_wifi_menu_on_key) {
      wifi_menu_show();
    }
    else {
      menu[1].visible = toc.is_ready() && !toc.is_empty();
      menu_viewer.show(menu, return_menu_index, false);
    }
  }

  static void
  restore_wifi_menu()
  {
    wifi_menu_show();
  }

  static void
  wifi_menu_back()
  {
    menu[1].visible = toc.is_ready() && !toc.is_empty();
    menu_viewer.show(menu, 6, false);
  }

  static void
  wifi_menu_edit_credentials()
  {
    wifi_show_setup_form(0, true, false);
  }

  static void
  wifi_menu_toggle_web_server()
  {
    if (is_web_server_running()) {
      stop_web_server();
      event_mgr.set_stay_on(false);

      msg_viewer.show(MsgViewer::MsgType::WIFI, false, true,
                      "Web Server Stopped",
                      "The web server has been stopped.");
      msg_viewer.auto_dismiss_in(7000, restore_wifi_menu);
      return;
    }

    if (!wifi_credentials_present()) {
      wifi_show_setup_form(0, true, true);
      return;
    }

    epub.close_file();
    fonts.clear(true);
    fonts.clear_glyph_caches();

    event_mgr.set_stay_on(true); // DO NOT sleep

    const bool started = start_web_server();
    if (!started) {
      event_mgr.set_stay_on(false);
    }

    msg_viewer.auto_dismiss_in(7000, restore_wifi_menu);
  }

#endif

static void
book_param_restore_menu()
{
  #if defined(BOARD_TYPE_PAPER_S3)
    menu[1].visible = toc.is_ready() && !toc.is_empty();
    menu_viewer.show(menu, 4, false);
  #else
    menu_viewer.show(menu);
  #endif
}

static void
book_param_about()
{
  CommonActions::about();
  #if defined(BOARD_TYPE_PAPER_S3)
    msg_viewer.auto_dismiss_in(7000, book_param_restore_menu);
  #endif
}

void
BookParamController::set_font_count(uint8_t count)
{
  book_params_form_entries[2].u.ch.choice_count = count;
}

void 
BookParamController::enter()
{
  menu[1].visible = toc.is_ready() && !toc.is_empty();
  #if defined(BOARD_TYPE_PAPER_S3)
    menu_viewer.show(menu, 0, true);
  #else
    menu_viewer.show(menu);
  #endif
  book_params_form_is_shown = false;
}

void 
BookParamController::leave(bool going_to_deep_sleep)
{

}

void 
BookParamController::input_event(const EventMgr::Event & event)
{
  #if EPUB_INKPLATE_BUILD && defined(BOARD_TYPE_PAPER_S3)
    if (wifi_setup_form_is_shown) {
      if (form_viewer.event(event)) {
        wifi_setup_form_is_shown = false;
        msg_viewer.show(MsgViewer::MsgType::CONFIRM, true, true,
                        "Save WiFi Credentials",
                        "Save these WiFi credentials and continue?");
        wifi_setup_confirm_is_shown = true;
      }
      return;
    }
    else if (wifi_setup_confirm_is_shown) {
      bool ok;
      if (msg_viewer.confirm(event, ok)) {
        wifi_setup_confirm_is_shown = false;

        if (!ok) {
          #if defined(BOARD_TYPE_PAPER_S3)
            wifi_return_to_menu(return_menu_index_on_key);
          #else
            menu_viewer.show(menu);
          #endif
          return;
        }

        if (wifi_ssid_buf[0] == 0) {
          wifi_show_setup_form(return_menu_index_on_key, wifi_return_to_wifi_menu_on_key, wifi_start_web_server_after_setup_on_key);
          return;
        }

        std::string ssid = wifi_ssid_buf;
        std::string pwd  = wifi_pwd_buf;
        config.put(Config::Ident::SSID, ssid);
        config.put(Config::Ident::PWD,  pwd );
        config.save(true);

        if (wifi_start_web_server_after_setup_on_key) {
          wifi_menu_toggle_web_server();
        }
        else {
          wifi_return_to_menu(return_menu_index_on_key);
        }
      }
      return;
    }
  #endif

  if (book_params_form_is_shown) {
    if (form_viewer.event(event)) {
      book_params_form_is_shown = false;
      // if (ok) {
        BookParams * book_params = epub.get_book_params();

        if (show_images       !=       old_show_images) book_params->put(BookParams::Ident::SHOW_IMAGES,        show_images      );
        if (font_size         !=         old_font_size) book_params->put(BookParams::Ident::FONT_SIZE,          font_size        );
        if (font              !=              old_font) book_params->put(BookParams::Ident::FONT,               font             );
        if (use_fonts_in_book != old_use_fonts_in_book) book_params->put(BookParams::Ident::USE_FONTS_IN_BOOK,  use_fonts_in_book);
        
        if (book_params->is_modified()) epub.update_book_format_params();

        book_params->save();

        if (old_use_fonts_in_book != use_fonts_in_book) {
          if (use_fonts_in_book) {
            epub.load_fonts();
          }
          else {
            fonts.clear();
            fonts.clear_glyph_caches();
          }
        }
 
        if (old_font != font) {
          fonts.adjust_default_font(font);
        }
     // }

      #if defined(BOARD_TYPE_PAPER_S3)
        // FormViewer clears the screen on completion for Paper S3; redraw the menu.
        menu[1].visible = toc.is_ready() && !toc.is_empty();
        menu_viewer.show(menu, 3, false);
      #else
        menu_viewer.clear_highlight();
      #endif
    }
  }
  else if (delete_current_book) {
    bool ok;
    if (msg_viewer.confirm(event, ok)) {
      if (ok) {
        std::string filepath = epub.get_current_filename();
        struct stat file_stat;

        if (stat(filepath.c_str(), &file_stat) != -1) {
          LOG_I("Deleting %s...", filepath.c_str());

          epub.close_file();
          unlink(filepath.c_str());

          int16_t pos = filepath.find_last_of('.');

          filepath.replace(pos, 5, ".pars");

          if (stat(filepath.c_str(), &file_stat) != -1) {
            LOG_I("Deleting file : %s", filepath.c_str());
            unlink(filepath.c_str());
          }

          filepath.replace(pos, 5, ".locs");

          if (stat(filepath.c_str(), &file_stat) != -1) {
            LOG_I("Deleting file : %s", filepath.c_str());
            unlink(filepath.c_str());
          }

          filepath.replace(pos, 5, ".toc");

          if (stat(filepath.c_str(), &file_stat) != -1) {
            LOG_I("Deleting file : %s", filepath.c_str());
            unlink(filepath.c_str());
          }

          int16_t dummy;
          books_dir.refresh(nullptr, dummy, false);

          app_controller.set_controller(AppController::Ctrl::DIR);
        }
      }
      else {
        msg_viewer.show(MsgViewer::MsgType::INFO, false, false, 
                        "Canceled", "The e-book was not deleted.");
        #if defined(BOARD_TYPE_PAPER_S3)
          msg_viewer.auto_dismiss_in(7000, book_param_restore_menu);
        #endif
      }
      delete_current_book = false;
    }
  }
  #if EPUB_INKPLATE_BUILD
    else if (wait_for_key_after_wifi) {
      wait_for_key_after_wifi = false;

      #if defined(BOARD_TYPE_PAPER_S3)
        if (stop_web_server_on_key) {
          stop_web_server();
          stop_web_server_on_key = false;
        }
        event_mgr.set_stay_on(false);
      #else
        msg_viewer.show(MsgViewer::MsgType::INFO, 
                        false, true, 
                        "Restarting", 
                        "The device is now restarting. Please wait.");
        stop_web_server();
        event_mgr.set_stay_on(false);
      #endif

      #if defined(BOARD_TYPE_PAPER_S3)
        menu[1].visible = toc.is_ready() && !toc.is_empty();
        menu_viewer.show(menu, return_menu_index_on_key, false);
      #else
        esp_restart();
      #endif
    }
  #endif
  else {
    if (menu_viewer.event(event)) {
      app_controller.set_controller(AppController::Ctrl::LAST);
    }
  }
}
