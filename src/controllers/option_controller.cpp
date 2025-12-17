// Copyright (c) 2020 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.

#define __OPTION_CONTROLLER__ 1
#include "controllers/option_controller.hpp"

#include "controllers/common_actions.hpp"
#include "controllers/app_controller.hpp"
#include "controllers/books_dir_controller.hpp"
#include "controllers/ntp.hpp"
#include "controllers/clock.hpp"
#include "viewers/menu_viewer.hpp"
#include "viewers/msg_viewer.hpp"
#include "viewers/form_viewer.hpp"
#include "models/books_dir.hpp"
#include "models/config.hpp"
#include "models/epub.hpp"
#include "models/nvs_mgr.hpp"

#include <cstring>

#if EPUB_INKPLATE_BUILD
  #include "esp_system.h"
#endif

// static int8_t boolean_value;

static Screen::Orientation     orientation;
 #if !defined(BOARD_TYPE_PAPER_S3)
 static Screen::PixelResolution  resolution;
 #endif

static int8_t show_battery;
static int8_t timeout;
static int8_t sleep_screen;
static int8_t show_images;
static int8_t font_size;
static int8_t use_fonts_in_books;
static int8_t default_font;
static int8_t show_title;
static int8_t dir_view;
static int8_t done;

#if EPUB_INKPLATE_BUILD && defined(BOARD_TYPE_PAPER_S3)
  enum class WifiPendingAction : uint8_t { NONE, WEB_SERVER, NTP };

  static WifiPendingAction wifi_pending_action = WifiPendingAction::NONE;
  static bool wifi_setup_form_is_shown = false;
  static bool wifi_setup_confirm_is_shown = false;

  static bool stop_web_server_on_key = false;
  static uint8_t return_menu_index_on_key = 0;
  static bool wifi_return_to_wifi_menu_on_key = false;

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
  wifi_show_setup_form(WifiPendingAction action, uint8_t return_menu_index, bool return_to_wifi_menu)
  {
    wifi_pending_action = action;
    return_menu_index_on_key = return_menu_index;
    wifi_return_to_wifi_menu_on_key = return_to_wifi_menu;
    wifi_load_buffers_from_config();
    wifi_done = 1;

    form_viewer.show(wifi_setup_form_entries, WIFI_FORM_SIZE, nullptr);
    wifi_setup_form_is_shown = true;
  }
#endif

static Screen::Orientation     old_orientation;
 #if !defined(BOARD_TYPE_PAPER_S3)
 static Screen::PixelResolution  old_resolution;
 #endif
static int8_t old_show_images;
static int8_t old_font_size;
static int8_t old_use_fonts_in_books;
static int8_t old_default_font;
static int8_t old_show_title;
static int8_t old_dir_view;
static int8_t old_sleep_screen;

#if DATE_TIME_RTC
  static int8_t show_heap_or_rtc;
  static uint16_t year;
  static uint16_t month, day, hour, minute, second;
#else
  static int8_t show_heap;
#endif

#if defined(BOARD_TYPE_PAPER_S3)
  // On Paper S3 the display is always driven in 4-bit grayscale via epdiy,
  // so the Pixel Resolution setting is not exposed in the UI.
  static constexpr int8_t MAIN_FORM_SIZE = 8;
#elif INKPLATE_6PLUS || TOUCH_TRIAL
  static constexpr int8_t MAIN_FORM_SIZE = 9;
#else
  static constexpr int8_t MAIN_FORM_SIZE = 8;
#endif

static FormEntry main_params_form_entries[MAIN_FORM_SIZE] = {
  { .caption = "Minutes Before Sleeping :",  .u = { .ch = { .value = &timeout,                .choice_count = 4, .choices = FormChoiceField::timeout_choices        } }, .entry_type = FormEntryType::HORIZONTAL  },
  { .caption = "Sleep Screen :",            .u = { .ch = { .value = &sleep_screen,           .choice_count = 2, .choices = FormChoiceField::sleep_screen_choices   } }, .entry_type = FormEntryType::HORIZONTAL  },
  { .caption = "Books Directory View :",     .u = { .ch = { .value = &dir_view,               .choice_count = 2, .choices = FormChoiceField::dir_view_choices       } }, .entry_type = FormEntryType::HORIZONTAL  },
  #if INKPLATE_6PLUS || TOUCH_TRIAL
    #if defined(BOARD_TYPE_PAPER_S3)
      { .caption = "uSDCard Position (triggers repagination):",    .u = { .ch = { .value = (int8_t *) &orientation, .choice_count = 4, .choices = FormChoiceField::orientation_choices    } }, .entry_type = FormEntryType::VERTICAL    },
    #else
      { .caption = "uSDCard Position (*):",               .u = { .ch = { .value = (int8_t *) &orientation, .choice_count = 4, .choices = FormChoiceField::orientation_choices    } }, .entry_type = FormEntryType::VERTICAL    },
    #endif
  #else
    #if defined(BOARD_TYPE_PAPER_S3)
      { .caption = "Buttons Position (triggers repagination):",    .u = { .ch = { .value = (int8_t *) &orientation, .choice_count = 3, .choices = FormChoiceField::orientation_choices    } }, .entry_type = FormEntryType::VERTICAL    },
    #else
      { .caption = "Buttons Position (*):",               .u = { .ch = { .value = (int8_t *) &orientation, .choice_count = 3, .choices = FormChoiceField::orientation_choices    } }, .entry_type = FormEntryType::VERTICAL    },
    #endif
  #endif
  #if !defined(BOARD_TYPE_PAPER_S3)
    { .caption = "Pixel Resolution :",         .u = { .ch = { .value = (int8_t *) &resolution,  .choice_count = 2, .choices = FormChoiceField::resolution_choices     } }, .entry_type = FormEntryType::HORIZONTAL  },
  #endif
  { .caption = "Show Battery Level :",       .u = { .ch = { .value = &show_battery,           .choice_count = 4, .choices = FormChoiceField::battery_visual_choices } }, .entry_type = FormEntryType::VERTICAL    },
  #if defined(BOARD_TYPE_PAPER_S3)
    { .caption = "Show Title (triggers repagination):",            .u = { .ch = { .value = &show_title,             .choice_count = 2, .choices = FormChoiceField::yes_no_choices         } }, .entry_type = FormEntryType::HORIZONTAL  },
  #else
    { .caption = "Show Title (*):",                       .u = { .ch = { .value = &show_title,             .choice_count = 2, .choices = FormChoiceField::yes_no_choices         } }, .entry_type = FormEntryType::HORIZONTAL  },
  #endif
  #if DATE_TIME_RTC
    { .caption = "Right Bottom Corner :",    .u = { .ch = { .value = &show_heap_or_rtc,       .choice_count = 3, .choices = FormChoiceField::right_corner_choices   } }, .entry_type = FormEntryType::VERTICAL    },
  #else
    { .caption = "Show Heap Sizes :",        .u = { .ch = { .value = &show_heap,              .choice_count = 2, .choices = FormChoiceField::yes_no_choices         } }, .entry_type = FormEntryType::HORIZONTAL  },
  #endif
  #if INKPLATE_6PLUS || TOUCH_TRIAL
    { .caption = " DONE ",                   .u = { .ch = { .value = &done,                   .choice_count = 0, .choices = nullptr                                 } }, .entry_type = FormEntryType::DONE        }
  #endif
};

#if INKPLATE_6PLUS || TOUCH_TRIAL
  static constexpr int8_t FONT_FORM_SIZE = 5;
#else
  static constexpr int8_t FONT_FORM_SIZE = 4;
#endif
static FormEntry font_params_form_entries[FONT_FORM_SIZE] = {
  #if defined(BOARD_TYPE_PAPER_S3)
    { .caption = "Default Font Size (default):",      .u = { .ch = { .value = &font_size,          .choice_count = 4, .choices = FormChoiceField::font_size_choices } }, .entry_type = FormEntryType::HORIZONTAL },
    { .caption = "Use Fonts in E-books (default):",   .u = { .ch = { .value = &use_fonts_in_books, .choice_count = 2, .choices = FormChoiceField::yes_no_choices    } }, .entry_type = FormEntryType::HORIZONTAL },
    { .caption = "Default Font (default):",           .u = { .ch = { .value = &default_font,       .choice_count = 8, .choices = FormChoiceField::font_choices      } }, .entry_type = FormEntryType::VERTICAL   },
    { .caption = "Show Images in E-books (default):", .u = { .ch = { .value = &show_images,        .choice_count = 2, .choices = FormChoiceField::yes_no_choices    } }, .entry_type = FormEntryType::HORIZONTAL },
  #else
    { .caption = "Default Font Size (*):",      .u = { .ch = { .value = &font_size,          .choice_count = 4, .choices = FormChoiceField::font_size_choices } }, .entry_type = FormEntryType::HORIZONTAL },
    { .caption = "Use Fonts in E-books (*):",   .u = { .ch = { .value = &use_fonts_in_books, .choice_count = 2, .choices = FormChoiceField::yes_no_choices    } }, .entry_type = FormEntryType::HORIZONTAL },
    { .caption = "Default Font (*):",           .u = { .ch = { .value = &default_font,       .choice_count = 8, .choices = FormChoiceField::font_choices      } }, .entry_type = FormEntryType::VERTICAL   },
    { .caption = "Show Images in E-books (*):", .u = { .ch = { .value = &show_images,        .choice_count = 2, .choices = FormChoiceField::yes_no_choices    } }, .entry_type = FormEntryType::HORIZONTAL },
  #endif
  #if INKPLATE_6PLUS || TOUCH_TRIAL
    { .caption = " DONE ",                    .u = { .ch = { .value = &done,               .choice_count = 0, .choices = nullptr                            } }, .entry_type = FormEntryType::DONE       }
  #endif
};

#if DATE_TIME_RTC
  #if defined(BOARD_TYPE_PAPER_S3)
    #if INKPLATE_6PLUS || TOUCH_TRIAL
      static constexpr int8_t DATE_TIME_FORM_SIZE = 3;
    #else
      static constexpr int8_t DATE_TIME_FORM_SIZE = 2;
    #endif

    static FormEntry date_time_form_entries[DATE_TIME_FORM_SIZE] = {
      { .caption = "Date:",
        .u = { .val3 = {
          .value0 = &year,  .min0 = 2022, .max0 = 2099, .label0 = "Year",
          .value1 = &month, .min1 =    1, .max1 =   12, .label1 = "Month",
          .value2 = &day,   .min2 =    1, .max2 =   31, .label2 = "Day"
        } },
        .entry_type = FormEntryType::UINT16_3 },
      { .caption = "Time (24h):",
        .u = { .val3 = {
          .value0 = &hour,   .min0 = 0, .max0 = 23, .label0 = "Hour",
          .value1 = &minute, .min1 = 0, .max1 = 59, .label1 = "Min",
          .value2 = &second, .min2 = 0, .max2 = 59, .label2 = "Sec"
        } },
        .entry_type = FormEntryType::UINT16_3 },

      #if INKPLATE_6PLUS || TOUCH_TRIAL
        { .caption = "DONE",   .u = { .ch  = { .value = &done,   .choice_count = 0, .choices = nullptr } }, .entry_type = FormEntryType::DONE    }
      #endif
    };
  #else
    #if INKPLATE_6PLUS || TOUCH_TRIAL
      static constexpr int8_t DATE_TIME_FORM_SIZE = 7;
    #else
      static constexpr int8_t DATE_TIME_FORM_SIZE = 6;
    #endif

    static FormEntry date_time_form_entries[DATE_TIME_FORM_SIZE] = {
      { .caption = "Year :",   .u = { .val = { .value = &year,   .min = 2022, .max = 2099 } }, .entry_type = FormEntryType::UINT16  },
      { .caption = "Month :",  .u = { .val = { .value = &month,  .min =    1, .max =   12 } }, .entry_type = FormEntryType::UINT16  },
      { .caption = "Day :",    .u = { .val = { .value = &day,    .min =    1, .max =   31 } }, .entry_type = FormEntryType::UINT16  },
      { .caption = "Hour :",         .u = { .val = { .value = &hour,   .min =    0, .max =   23 } }, .entry_type = FormEntryType::UINT16  },
      { .caption = "Minute :", .u = { .val = { .value = &minute, .min =    0, .max =   59 } }, .entry_type = FormEntryType::UINT16  },
      { .caption = "Second :", .u = { .val = { .value = &second, .min =    0, .max =   59 } }, .entry_type = FormEntryType::UINT16  },

      #if INKPLATE_6PLUS || TOUCH_TRIAL
        { .caption = "DONE",   .u = { .ch  = { .value = &done,   .choice_count = 0, .choices = nullptr } }, .entry_type = FormEntryType::DONE    }
      #endif
    };
  #endif
#endif

static void restore_option_menu();
static void option_about();

extern bool start_web_server();
extern void stop_web_server();
extern bool is_web_server_running();

#if EPUB_INKPLATE_BUILD && defined(BOARD_TYPE_PAPER_S3)
  static void wifi_menu_show();
  static void restore_wifi_menu();
  static void wifi_menu_edit_credentials();
  static void wifi_menu_toggle_web_server();
  static void wifi_start_web_server_after_setup();
  static void wifi_return_to_menu(uint8_t return_menu_index);
#endif

static void
main_parameters()
{
  config.get(Config::Ident::ORIENTATION,      (int8_t *) &orientation);
  config.get(Config::Ident::DIR_VIEW,         &dir_view              );
  #if !defined(BOARD_TYPE_PAPER_S3)
    config.get(Config::Ident::PIXEL_RESOLUTION, (int8_t *) &resolution );
  #endif
  config.get(Config::Ident::BATTERY,          &show_battery          );
  config.get(Config::Ident::SHOW_TITLE,       &show_title            );
  config.get(Config::Ident::TIMEOUT,          &timeout               );
  config.get(Config::Ident::SLEEP_SCREEN,     &sleep_screen          );

  #if DATE_TIME_RTC
    int8_t show_heap, show_rtc;
    config.get(Config::Ident::SHOW_RTC,       &show_rtc              );
    config.get(Config::Ident::SHOW_HEAP,      &show_heap             );

    show_heap_or_rtc = (show_rtc != 0) ? 1 : ((show_heap != 0) ? 2 : 0);
  #else
    config.get(Config::Ident::SHOW_HEAP,      &show_heap             );
  #endif

  old_orientation = orientation;
  old_dir_view    = dir_view;
  #if !defined(BOARD_TYPE_PAPER_S3)
    old_resolution  = resolution;
  #endif
  old_show_title  = show_title;
  old_sleep_screen = sleep_screen;
  done            = 1;

  form_viewer.show(
    main_params_form_entries, 
    MAIN_FORM_SIZE, 
    #if defined(BOARD_TYPE_PAPER_S3)
      nullptr);
    #else
      "(*) Will trigger e-book pages location recalc.");
    #endif

  option_controller.set_main_form_is_shown();
}

static void
default_parameters()
{
  config.get(Config::Ident::SHOW_IMAGES,        &show_images       );
  config.get(Config::Ident::FONT_SIZE,          &font_size         );
  config.get(Config::Ident::USE_FONTS_IN_BOOKS, &use_fonts_in_books);
  config.get(Config::Ident::DEFAULT_FONT,       &default_font      );
  
  old_show_images        = show_images;
  old_use_fonts_in_books = use_fonts_in_books;
  old_default_font       = default_font;
  old_font_size          = font_size;
  done                   = 1;

  form_viewer.show(
    font_params_form_entries, 
    FONT_FORM_SIZE, 
    #if defined(BOARD_TYPE_PAPER_S3)
      nullptr);
    #else
      "(*) Used as e-book default values.");
    #endif

  option_controller.set_font_form_is_shown();
}

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
    return_menu_index_on_key = 4;
    option_controller.set_wait_for_key_after_wifi();
  #endif
}

static void
init_nvs()
{
  menu_viewer.clear_highlight();
  #if EPUB_INKPLATE_BUILD
    if (nvs_mgr.setup(true)) {
      msg_viewer.show(
        MsgViewer::MsgType::BOOK, 
        false,
        false,
        "E-Books History Cleared", 
        "The E-Books History has been initialized with success.");

      msg_viewer.auto_dismiss_in(7000, restore_option_menu);
    }
    else {
      msg_viewer.show(
        MsgViewer::MsgType::BOOK, 
        false,
        false,
        "E-Books History Clearing Error", 
        "The E-Books History has not been initialized properly. "
        "Potential hardware problem or software framework issue.");

      msg_viewer.auto_dismiss_in(7000, restore_option_menu);
    }
  #endif
}

#if INKPLATE_6PLUS || MENU_6PLUS
  static void goto_next();
  static void goto_prev();
#endif

#if INKPLATE_6PLUS
  static void 
  calibrate()
  {
    event_mgr.show_calibration();
    option_controller.set_calibration_is_shown();
  }
#endif

#if DATE_TIME_RTC
  static void 
  clock_adjust_form()
  {
    time_t t;
    tm tim;
    Clock::get_date_time(t);

    localtime_r(&t, &tim);

    year   = tim.tm_year + 1900;
    month  = tim.tm_mon + 1;
    day    = tim.tm_mday;
    hour   = tim.tm_hour;
    minute = tim.tm_min;
    second = tim.tm_sec;

    form_viewer.show(date_time_form_entries, DATE_TIME_FORM_SIZE,
      #if defined(BOARD_TYPE_PAPER_S3)
        nullptr);
      #else
        "Hour is in 24 hours format.");
      #endif
    option_controller.set_date_time_form_is_shown();
  }

  static void
  set_clock()
  {
    time_t t;
    tm tim;

    tim = {
      .tm_sec   = second,
      .tm_min   = minute,
      .tm_hour  = hour,
      .tm_mday  = day,
      .tm_mon   = month - 1,
      .tm_year  = year - 1900,
      .tm_wday  = 0,
      .tm_yday  = 0,
      .tm_isdst = -1
    };

    t = mktime(&tim);
    Clock::set_date_time(t);
  }

  static void
  ntp_clock_adjust()
  {
    page_locs.abort_threads();
    epub.close_file();

    #if EPUB_INKPLATE_BUILD && defined(BOARD_TYPE_PAPER_S3)
      if (!wifi_credentials_present()) {
        wifi_show_setup_form(WifiPendingAction::NTP, 8, false);
        return;
      }
    #endif

    std::string ntp_server;
    config.get(Config::Ident::NTP_SERVER, ntp_server);

    msg_viewer.show(MsgViewer::MsgType::NTP_CLOCK, false, true, 
      "Date/Time Retrival", 
      "Retrieving Date and Time from NTP Server %s. Please wait.",
      ntp_server.c_str());

    if (ntp.get_and_set_time()) {
      time_t time;
      Clock::get_date_time(time);
      msg_viewer.show(MsgViewer::MsgType::NTP_CLOCK, true, true,
        "Date/Time Retrival Completed",
        "Local Time is %s.", ctime(&time));
    }
    else {
      msg_viewer.show(MsgViewer::MsgType::NTP_CLOCK, true, true,
        "Date/Time Retrival Failed",
        "Unable to get Date/Time from NTP Server! Please verify WiFi and server settings.");
    }

    #if EPUB_INKPLATE_BUILD && defined(BOARD_TYPE_PAPER_S3)
      stop_web_server_on_key = false;
      return_menu_index_on_key = 8;
    #endif

    option_controller.set_wait_for_key_after_wifi();
  }
#endif

#if EPUB_LINUX_BUILD && DEBUGGING
  void
  debugging()
  {
    #if DATE_TIME_RTC
      clock_adjust_form();
    #endif
  }
#endif

// IMPORTANT!!!
// The first (menu[0]) and the last menu entry (the one before END_MENU) MUST ALWAYS BE VISIBLE!!!

static MenuViewer::MenuEntry menu[] = {

  { MenuViewer::Icon::RETURN,        "Return to the e-books list",           CommonActions::return_to_last    , true,  true  },
  { MenuViewer::Icon::BOOK,          "Return to the last e-book being read", CommonActions::show_last_book    , true,  true  },
  { MenuViewer::Icon::MAIN_PARAMS,   "Main parameters",                      main_parameters                  , true,  true  },
  { MenuViewer::Icon::FONT_PARAMS,   "Default e-books parameters",           default_parameters               , true,  true  },
  { MenuViewer::Icon::WIFI,          "WiFi Access to the e-books folder",    wifi_mode                        , true,  true  },
  { MenuViewer::Icon::REFRESH,       "Refresh the e-books list",             CommonActions::refresh_books_dir , true,  true  },
  #if !(INKPLATE_6PLUS || MENU_6PLUS)
    { MenuViewer::Icon::CLR_HISTORY, "Clear e-books' read history",          init_nvs                         , true,  true  },
    #if DATE_TIME_RTC
      { MenuViewer::Icon::CLOCK,     "Set Date/Time",                        clock_adjust_form                , true,  true  },
      { MenuViewer::Icon::NTP_CLOCK, "Retrieve Date/Time from Time Server",  ntp_clock_adjust                 , true,  true  },
    #endif
  #endif
  #if EPUB_LINUX_BUILD && DEBUGGING
    { MenuViewer::Icon::DEBUG,       "Debugging",                            debugging                        , true,  true  },
  #endif
  { MenuViewer::Icon::INFO,          "About the EPub-InkPlate application",  option_about                     , true,  true  },
  { MenuViewer::Icon::POWEROFF,      "Power OFF (Deep Sleep)",               CommonActions::power_it_off      , true,  true  },
  #if INKPLATE_6PLUS || MENU_6PLUS
    { MenuViewer::Icon::NEXT_MENU,   "Other options",                        goto_next                        , true,  true  },
  #endif
  { MenuViewer::Icon::END_MENU,       nullptr,                               nullptr                          , false, false }
};

static void
restore_option_menu()
{
  #if defined(BOARD_TYPE_PAPER_S3)
    menu_viewer.show(menu, 0, false);
  #else
    menu_viewer.show(menu);
  #endif
}

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
    menu_viewer.show(menu, 4, false);
  }

  static void
  wifi_menu_edit_credentials()
  {
    wifi_show_setup_form(WifiPendingAction::NONE, 0, true);
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
      wifi_show_setup_form(WifiPendingAction::WEB_SERVER, 0, true);
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

  static void
  wifi_start_web_server_after_setup()
  {
    wifi_menu_toggle_web_server();
  }
#endif

static void
option_about()
{
  CommonActions::about();
  msg_viewer.auto_dismiss_in(7000, restore_option_menu);
}

#if INKPLATE_6PLUS
static MenuViewer::MenuEntry sub_menu[] = {
  { MenuViewer::Icon::PREV_MENU,     "Previous options",                     goto_prev                        , true,  true  },
  { MenuViewer::Icon::RETURN,        "Return to the e-books list",           CommonActions::return_to_last    , true,  true  },
  #if DATE_TIME_RTC
    { MenuViewer::Icon::CLOCK,       "Set Date/Time",                        clock_adjust_form                , true,  true  },
    { MenuViewer::Icon::NTP_CLOCK,   "Retrieve Date/Time from Time Server",  ntp_clock_adjust                 , true,  true  },
  #endif
  { MenuViewer::Icon::CALIB,         "Touch Screen Calibration",             calibrate                        , true,  false },
  { MenuViewer::Icon::CLR_HISTORY,   "Clear e-books' read history",          init_nvs                         , true,  true  },
  { MenuViewer::Icon::END_MENU,       nullptr,                               nullptr                          , false, false }
};
#elif MENU_6PLUS
static MenuViewer::MenuEntry sub_menu[] = {
  { MenuViewer::Icon::PREV_MENU,     "Previous options",                     goto_prev                        , true,  true  },
  { MenuViewer::Icon::RETURN,        "Return to the e-books list",           nullptr                          , true,  true  },
  #if DATE_TIME_RTC
    { MenuViewer::Icon::CLOCK,       "Set Date/Time",                        nullptr                          , true,  true  },
    { MenuViewer::Icon::NTP_CLOCK,   "Retrieve Date/Time from Time Server",  nullptr                          , true,  true  },
  #endif
  { MenuViewer::Icon::CALIB,         "Touch Screen Calibration",             nullptr                          , true,  false },
  { MenuViewer::Icon::CLR_HISTORY,   "Clear e-books' read history",          nullptr                          , true,  true  },
  { MenuViewer::Icon::END_MENU,       nullptr,                               nullptr                          , false, false }
};
#endif

void
OptionController::set_font_count(uint8_t count)
{
  font_params_form_entries[2].u.ch.choice_count = count;
}

void 
OptionController::enter()
{
  #if defined(BOARD_TYPE_PAPER_S3)
    menu_viewer.show(menu, 0, true);
  #else
    menu_viewer.show(menu);
  #endif
  main_form_is_shown = false;
  font_form_is_shown = false;
}

#if INKPLATE_6PLUS || MENU_6PLUS
  static void 
  goto_next()
  {
    menu_viewer.show(sub_menu);
  }

  static void 
  goto_prev()
  {
    menu_viewer.show(menu);
  }
#endif

void 
OptionController::leave(bool going_to_deep_sleep)
{
}

void 
OptionController::input_event(const EventMgr::Event & event)
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
          wifi_pending_action = WifiPendingAction::NONE;
          wifi_return_to_menu(return_menu_index_on_key);
          return;
        }

        if (wifi_ssid_buf[0] == 0) {
          wifi_show_setup_form(wifi_pending_action, return_menu_index_on_key, wifi_return_to_wifi_menu_on_key);
          return;
        }

        std::string ssid = wifi_ssid_buf;
        std::string pwd  = wifi_pwd_buf;
        config.put(Config::Ident::SSID, ssid);
        config.put(Config::Ident::PWD,  pwd );
        config.save(true);

        WifiPendingAction action = wifi_pending_action;
        wifi_pending_action = WifiPendingAction::NONE;

        if (action == WifiPendingAction::WEB_SERVER) {
          wifi_start_web_server_after_setup();
        }
        else if (action == WifiPendingAction::NTP) {
          ntp_clock_adjust();
        }
        else {
          wifi_return_to_menu(return_menu_index_on_key);
        }
      }
      return;
    }
  #endif

  if (main_form_is_shown) {
    if (form_viewer.event(event)) {
      main_form_is_shown = false;
      // if (ok) {
        config.put(Config::Ident::ORIENTATION,      (int8_t) orientation);
        config.put(Config::Ident::DIR_VIEW,         dir_view            );
        #if !defined(BOARD_TYPE_PAPER_S3)
          config.put(Config::Ident::PIXEL_RESOLUTION, (int8_t) resolution );
        #endif
        config.put(Config::Ident::BATTERY,          show_battery        );
        config.put(Config::Ident::SHOW_TITLE,       show_title          );
        config.put(Config::Ident::TIMEOUT,          timeout             );
        config.put(Config::Ident::SLEEP_SCREEN,     sleep_screen        );

        #if DATE_TIME_RTC
          config.put(Config::Ident::SHOW_HEAP,      (int8_t)(show_heap_or_rtc == 2 ? 1 : 0));
          config.put(Config::Ident::SHOW_RTC,       (int8_t)(show_heap_or_rtc == 1 ? 1 : 0));
        #else
          config.put(Config::Ident::SHOW_HEAP,      show_heap           );
        #endif

        config.save();

        if (old_orientation != orientation) {
          screen.set_orientation(orientation);
          event_mgr.set_orientation(orientation);
          books_dir_controller.new_orientation();
        }

        if (old_dir_view != dir_view) {
          books_dir_controller.set_current_book_index(-1);
        }
        
        #if !defined(BOARD_TYPE_PAPER_S3)
          if (old_resolution != resolution) {
            fonts.clear_glyph_caches();
            screen.set_pixel_resolution(resolution);
          }
        #endif

        if ((old_orientation != orientation) ||
            (old_show_title  != show_title )) {
          epub.update_book_format_params();
        }

        #if !defined(BOARD_TYPE_PAPER_S3)
          if ((old_orientation != orientation) || 
              (old_resolution  != resolution )) {
        #else
          if (old_orientation != orientation) {
        #endif
          menu_viewer.show(menu, 2, true);
        }
        else {
          #if defined(BOARD_TYPE_PAPER_S3)
            menu_viewer.show(menu, 2, false);
          #else
            menu_viewer.clear_highlight();
          #endif
        }
      // }
    }
  }
  else if (font_form_is_shown) {
    if (form_viewer.event(event)) {
      font_form_is_shown = false;
      // if (ok) {
        config.put(Config::Ident::SHOW_IMAGES,        show_images       );
        config.put(Config::Ident::FONT_SIZE,          font_size         );
        config.put(Config::Ident::DEFAULT_FONT,       default_font      );
        config.put(Config::Ident::USE_FONTS_IN_BOOKS, use_fonts_in_books);
        config.save();

        if ((old_show_images        != show_images       ) ||
            (old_font_size          != font_size         ) ||
            (old_default_font       != default_font      ) ||
            (old_use_fonts_in_books != use_fonts_in_books)) {
          epub.update_book_format_params();  
        }

        if (old_default_font != default_font) {
          fonts.adjust_default_font(default_font);
        }

        if (old_use_fonts_in_books != use_fonts_in_books) {
          if (use_fonts_in_books == 0) {
            fonts.clear();
            fonts.clear_glyph_caches();
          }
        }
      // }
      #if defined(BOARD_TYPE_PAPER_S3)
        menu_viewer.show(menu, 3, false);
      #else
        menu_viewer.clear_highlight();
      #endif
    }
  }

  #if DATE_TIME_RTC
    else if (date_time_form_is_shown) {
      if (form_viewer.event(event)) {
        date_time_form_is_shown = false;
        #if defined(BOARD_TYPE_PAPER_S3)
          menu_viewer.show(menu, 7, false);
        #else
          menu_viewer.clear_highlight();
        #endif
        set_clock();
      }
    }
  #endif

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

      if (books_refresh_needed) {
        books_refresh_needed = false;
        int16_t dummy;
        books_dir.refresh(nullptr, dummy, true);
      }

      #if defined(BOARD_TYPE_PAPER_S3)
        menu_viewer.show(menu, return_menu_index_on_key, false);
      #else
        esp_restart();
      #endif
    }
  #endif

  #if INKPLATE_6PLUS
  else if (calibration_is_shown) {
    if (event_mgr.calibration_event(event)) {
      calibration_is_shown = false;
      menu_viewer.show(menu, 0, true);
    }
  }
  #endif
  
  else {
    if (menu_viewer.event(event)) {
      if (books_refresh_needed) {
        books_refresh_needed = false;
        int16_t dummy;
        books_dir.refresh(nullptr, dummy, true);
      }
      app_controller.set_controller(AppController::Ctrl::LAST);
    }
  }
}
