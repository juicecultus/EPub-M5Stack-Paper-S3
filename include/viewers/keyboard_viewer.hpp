// Copyright (c) 2020 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.

#pragma once
#include "global.hpp"

#include "controllers/event_mgr.hpp"
#include "models/fonts.hpp"
#include "screen.hpp"
#include "viewers/page.hpp"

#if INKPLATE_6PLUS || TOUCH_TRIAL

/**
 * @brief Message presentation class
 * 
 * This class supply a screen keyboard to be used with a touch screen.
 * 
 */
class KeyboardViewer
{
  private:
    static constexpr char const * TAG = "KeyboardViewer";

    enum class KBType : int8_t { ALFA, ALFA_SHIFTED, NUMBERS, SPECIAL };
    enum class KeyCode : int16_t {
      SHIFT = -1,
      BACKSPACE = -2,
      SPACE = -3,
      MODE_ABC = -4,
      MODE_123 = -5,
      MODE_SPECIAL = -6,
      OK = -7,
      CANCEL = -8
    };

    struct Key {
      Pos pos;
      Dim dim;
      int16_t code;
      const char * label;
    };

    static constexpr uint8_t MAX_KEYS = 64;
    static constexpr uint8_t FONT_SIZE = 14;
    static constexpr uint8_t LABEL_FONT_SIZE = 10;
    static constexpr uint8_t VALUE_FONT_SIZE = 12;

    Page::Format fmt;
    KBType current_kb_type;

    char * client_buf = nullptr;
    uint16_t client_buf_len = 0;
    uint16_t client_len = 0;
    bool password_mode = false;

    char original_buf[96];
    const char * caption = nullptr;

    Key keys[MAX_KEYS];
    uint8_t key_count = 0;

    Pos modal_pos;
    Dim modal_dim;
    Pos value_pos;
    Dim value_dim;

    void clamp_client_len();
    void set_kb_type(KBType kb_type);
    void add_key(Pos pos, Dim dim, int16_t code, const char * label);
    const Key * find_key(uint16_t x, uint16_t y) const;

    void draw_static();
    void draw_value();
    void draw_keys();

    void append_char(char ch);
    void backspace();

  public:
    KeyboardViewer() : current_kb_type(KBType::ALFA) {};

    void show(char * str, uint16_t len, const char * _caption, bool is_password);
    bool event(const EventMgr::Event & event);

    const char * get_value() const { return (client_buf != nullptr) ? client_buf : ""; }
};

#else

class KeyboardViewer
{
  public:
    void show(char * str, uint16_t len, const char * _caption, bool is_password) {
      (void)str;
      (void)len;
      (void)_caption;
      (void)is_password;
    }

    bool event(const EventMgr::Event & event) {
      (void)event;
      return false;
    }

    const char * get_value() const { return ""; }
};

#endif

#if __KEYBOARD_VIEWER__
  KeyboardViewer keyboard_viewer;
#else
  extern KeyboardViewer keyboard_viewer;
#endif