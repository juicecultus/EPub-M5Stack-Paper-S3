// Copyright (c) 2020 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.

#define __KEYBOARD_VIEWER__ 1
#include "viewers/keyboard_viewer.hpp"

#if INKPLATE_6PLUS || TOUCH_TRIAL

#include "viewers/page.hpp"
#include "models/fonts.hpp"
#include "screen.hpp"

#include <cstdio>
#include <cstring>

static inline bool in_rect(uint16_t x, uint16_t y, Pos pos, Dim dim)
{
  return (x >= (uint16_t)pos.x) && (y >= (uint16_t)pos.y) &&
         (x <= (uint16_t)(pos.x + dim.width)) && (y <= (uint16_t)(pos.y + dim.height));
}

void
KeyboardViewer::clamp_client_len()
{
  if (client_buf == nullptr) return;
  if (client_buf_len == 0) return;
  if (client_len >= client_buf_len) client_len = (uint16_t)(client_buf_len - 1);
  client_buf[client_len] = 0;
}

void
KeyboardViewer::add_key(Pos pos, Dim dim, int16_t code, const char * label)
{
  if (key_count >= MAX_KEYS) return;
  keys[key_count].pos = pos;
  keys[key_count].dim = dim;
  keys[key_count].code = code;
  keys[key_count].label = label;
  key_count++;
}

const KeyboardViewer::Key *
KeyboardViewer::find_key(uint16_t x, uint16_t y) const
{
  for (uint8_t i = 0; i < key_count; i++) {
    if (in_rect(x, y, keys[i].pos, keys[i].dim)) return &keys[i];
  }
  return nullptr;
}

void
KeyboardViewer::append_char(char ch)
{
  if (client_buf == nullptr) return;
  if (client_buf_len == 0) return;
  if (client_len >= (uint16_t)(client_buf_len - 1)) return;
  client_buf[client_len++] = ch;
  clamp_client_len();
}

void
KeyboardViewer::backspace()
{
  if (client_buf == nullptr) return;
  if (client_len == 0) return;
  client_len--;
  clamp_client_len();
}

void
KeyboardViewer::set_kb_type(KBType kb_type)
{
  current_kb_type = kb_type;
  key_count = 0;

  const int16_t pad = 12;
  const int16_t gap = 6;

  const int16_t x0 = (int16_t)(modal_pos.x + pad);
  const int16_t w_avail = (int16_t)(modal_dim.width - (pad * 2));
  int16_t y = (int16_t)(value_pos.y + value_dim.height + 12);

  const int16_t key_h = 56;

  auto add_row = [&](const char * chars, uint8_t n, int16_t y_row, bool shifted) {
    const int16_t key_w = (int16_t)((w_avail - (int16_t)((n - 1) * gap)) / n);
    int16_t x = x0;
    for (uint8_t i = 0; i < n; i++) {
      char ch = chars[i];
      if (shifted && (ch >= 'a') && (ch <= 'z')) ch = (char)('A' + (ch - 'a'));
      add_key(Pos(x, y_row), Dim(key_w, key_h), (int16_t)ch, nullptr);
      x = (int16_t)(x + key_w + gap);
    }
  };

  if ((kb_type == KBType::ALFA) || (kb_type == KBType::ALFA_SHIFTED)) {
    const bool shifted = (kb_type == KBType::ALFA_SHIFTED);

    const char * row1 = "qwertyuiop";
    const char * row2 = "asdfghjkl";
    const char * row3 = "zxcvbnm";

    add_row(row1, 10, y, shifted);
    y = (int16_t)(y + key_h + gap);

    {
      const uint8_t n = 9;
      const int16_t key_w = (int16_t)((w_avail - (int16_t)((n - 1) * gap)) / n);
      const int16_t indent = (int16_t)((w_avail - ((key_w * n) + (gap * (n - 1)))) / 2);
      int16_t x = (int16_t)(x0 + indent);
      for (uint8_t i = 0; i < n; i++) {
        char ch = row2[i];
        if (shifted) ch = (char)('A' + (ch - 'a'));
        add_key(Pos(x, y), Dim(key_w, key_h), (int16_t)ch, nullptr);
        x = (int16_t)(x + key_w + gap);
      }
    }
    y = (int16_t)(y + key_h + gap);

    {
      const int16_t shift_w = 84;
      const int16_t bsp_w = 96;
      const int16_t mid_w = (int16_t)(w_avail - shift_w - bsp_w - (gap * 2));
      const uint8_t n = 7;
      const int16_t key_w = (int16_t)((mid_w - (int16_t)((n - 1) * gap)) / n);
      int16_t x = x0;
      add_key(Pos(x, y), Dim(shift_w, key_h), (int16_t)KeyCode::SHIFT, "SHIFT");
      x = (int16_t)(x + shift_w + gap);
      for (uint8_t i = 0; i < n; i++) {
        char ch = row3[i];
        if (shifted) ch = (char)('A' + (ch - 'a'));
        add_key(Pos(x, y), Dim(key_w, key_h), (int16_t)ch, nullptr);
        x = (int16_t)(x + key_w + gap);
      }
      add_key(Pos(x, y), Dim(bsp_w, key_h), (int16_t)KeyCode::BACKSPACE, "BSP");
    }
    y = (int16_t)(y + key_h + gap);

    {
      const int16_t mode_w = 84;
      const int16_t spec_w = 84;
      const int16_t ok_w = 96;
      const int16_t cancel_w = 120;
      const int16_t space_w = (int16_t)(w_avail - mode_w - spec_w - ok_w - cancel_w - (gap * 4));
      int16_t x = x0;
      add_key(Pos(x, y), Dim(mode_w, key_h), (int16_t)KeyCode::MODE_123, "123");
      x = (int16_t)(x + mode_w + gap);
      add_key(Pos(x, y), Dim(spec_w, key_h), (int16_t)KeyCode::MODE_SPECIAL, "#+=");
      x = (int16_t)(x + spec_w + gap);
      add_key(Pos(x, y), Dim(space_w, key_h), (int16_t)KeyCode::SPACE, "SPACE");
      x = (int16_t)(x + space_w + gap);
      add_key(Pos(x, y), Dim(ok_w, key_h), (int16_t)KeyCode::OK, "OK");
      x = (int16_t)(x + ok_w + gap);
      add_key(Pos(x, y), Dim(cancel_w, key_h), (int16_t)KeyCode::CANCEL, "CANCEL");
    }
  }
  else if (kb_type == KBType::NUMBERS) {
    const char * row1 = "1234567890";
    const char * row2 = "-/:;()$&@\"";
    add_row(row1, 10, y, false);
    y = (int16_t)(y + key_h + gap);
    add_row(row2, 10, y, false);
    y = (int16_t)(y + key_h + gap);

    {
      const int16_t mode_w = 84;
      const int16_t bsp_w = 96;
      const int16_t mid_w = (int16_t)(w_avail - mode_w - bsp_w - (gap * 2));
      const uint8_t n = 5;
      const int16_t key_w = (int16_t)((mid_w - (int16_t)((n - 1) * gap)) / n);
      int16_t x = x0;
      add_key(Pos(x, y), Dim(mode_w, key_h), (int16_t)KeyCode::MODE_SPECIAL, "#+=");
      x = (int16_t)(x + mode_w + gap);
      const char * mid = ".,?!'";
      for (uint8_t i = 0; i < n; i++) {
        add_key(Pos(x, y), Dim(key_w, key_h), (int16_t)mid[i], nullptr);
        x = (int16_t)(x + key_w + gap);
      }
      add_key(Pos(x, y), Dim(bsp_w, key_h), (int16_t)KeyCode::BACKSPACE, "BSP");
    }
    y = (int16_t)(y + key_h + gap);

    {
      const int16_t mode_w = 84;
      const int16_t ok_w = 96;
      const int16_t cancel_w = 120;
      const int16_t space_w = (int16_t)(w_avail - mode_w - ok_w - cancel_w - (gap * 3));
      int16_t x = x0;
      add_key(Pos(x, y), Dim(mode_w, key_h), (int16_t)KeyCode::MODE_ABC, "ABC");
      x = (int16_t)(x + mode_w + gap);
      add_key(Pos(x, y), Dim(space_w, key_h), (int16_t)KeyCode::SPACE, "SPACE");
      x = (int16_t)(x + space_w + gap);
      add_key(Pos(x, y), Dim(ok_w, key_h), (int16_t)KeyCode::OK, "OK");
      x = (int16_t)(x + ok_w + gap);
      add_key(Pos(x, y), Dim(cancel_w, key_h), (int16_t)KeyCode::CANCEL, "CANCEL");
    }
  }
  else {
    const char * row1 = "[]{}#%^*+=";
    const char * row2 = "_\\|~<>`/\"@";
    add_row(row1, 10, y, false);
    y = (int16_t)(y + key_h + gap);
    add_row(row2, 10, y, false);
    y = (int16_t)(y + key_h + gap);

    {
      const int16_t mode_w = 84;
      const int16_t bsp_w = 96;
      const int16_t mid_w = (int16_t)(w_avail - mode_w - bsp_w - (gap * 2));
      const uint8_t n = 5;
      const int16_t key_w = (int16_t)((mid_w - (int16_t)((n - 1) * gap)) / n);
      int16_t x = x0;
      add_key(Pos(x, y), Dim(mode_w, key_h), (int16_t)KeyCode::MODE_123, "123");
      x = (int16_t)(x + mode_w + gap);
      const char * mid = ".,?!'";
      for (uint8_t i = 0; i < n; i++) {
        add_key(Pos(x, y), Dim(key_w, key_h), (int16_t)mid[i], nullptr);
        x = (int16_t)(x + key_w + gap);
      }
      add_key(Pos(x, y), Dim(bsp_w, key_h), (int16_t)KeyCode::BACKSPACE, "BSP");
    }
    y = (int16_t)(y + key_h + gap);

    {
      const int16_t mode_w = 84;
      const int16_t ok_w = 96;
      const int16_t cancel_w = 120;
      const int16_t space_w = (int16_t)(w_avail - mode_w - ok_w - cancel_w - (gap * 3));
      int16_t x = x0;
      add_key(Pos(x, y), Dim(mode_w, key_h), (int16_t)KeyCode::MODE_ABC, "ABC");
      x = (int16_t)(x + mode_w + gap);
      add_key(Pos(x, y), Dim(space_w, key_h), (int16_t)KeyCode::SPACE, "SPACE");
      x = (int16_t)(x + space_w + gap);
      add_key(Pos(x, y), Dim(ok_w, key_h), (int16_t)KeyCode::OK, "OK");
      x = (int16_t)(x + ok_w + gap);
      add_key(Pos(x, y), Dim(cancel_w, key_h), (int16_t)KeyCode::CANCEL, "CANCEL");
    }
  }
}

void
KeyboardViewer::draw_static()
{
  page.start(fmt);

  page.clear_region(Dim((int16_t)(modal_dim.width + 20), (int16_t)(modal_dim.height + 20)),
                    Pos((int16_t)(modal_pos.x - 10), (int16_t)(modal_pos.y - 10)));

  page.put_highlight(Dim((int16_t)(modal_dim.width + 14), (int16_t)(modal_dim.height + 14)),
                     Pos((int16_t)(modal_pos.x - 7), (int16_t)(modal_pos.y - 7)));

  page.put_rounded(modal_dim, modal_pos);

  Page::Format label_fmt = fmt;
  label_fmt.font_size = LABEL_FONT_SIZE;
  label_fmt.align = CSS::Align::LEFT;
  if (caption != nullptr) {
    page.put_str_at(caption, Pos((int16_t)(modal_pos.x + 14), (int16_t)(modal_pos.y + 18)), label_fmt);
  }

  page.put_rounded(value_dim, value_pos);
}

void
KeyboardViewer::draw_value()
{
  page.start(fmt);

  page.clear_region(Dim((int16_t)(value_dim.width - 8), (int16_t)(value_dim.height - 8)),
                    Pos((int16_t)(value_pos.x + 4), (int16_t)(value_pos.y + 4)));

  Page::Format vfmt = fmt;
  vfmt.font_size = VALUE_FONT_SIZE;
  vfmt.align = CSS::Align::LEFT;

  const int16_t max_w = (int16_t)(value_dim.width - 20);
  Font * font = fonts.get(1);
  if (font == nullptr) return;

  char shown[96];
  if (password_mode) {
    uint16_t n = client_len;
    if (n > (uint16_t)(sizeof(shown) - 1)) n = (uint16_t)(sizeof(shown) - 1);
    for (uint16_t i = 0; i < n; i++) shown[i] = '*';
    shown[n] = 0;
  }
  else {
    if (client_buf == nullptr) {
      shown[0] = 0;
    }
    else {
      (void)snprintf(shown, sizeof(shown), "%s", client_buf);
    }
  }

  Dim dim;
  font->get_size(shown, &dim, VALUE_FONT_SIZE);
  if (dim.width > max_w) {
    const char * src = shown;
    size_t len = strlen(src);
    if (len > 3) {
      for (size_t s = 0; s < len; s++) {
        char tmp[96];
        (void)snprintf(tmp, sizeof(tmp), "...%s", src + s);
        font->get_size(tmp, &dim, VALUE_FONT_SIZE);
        if (dim.width <= max_w) {
          (void)snprintf(shown, sizeof(shown), "%s", tmp);
          break;
        }
      }
    }
  }

  page.put_str_at(shown, Pos((int16_t)(value_pos.x + 12), (int16_t)(value_pos.y + (value_dim.height >> 1) + 8)), vfmt);
}

void
KeyboardViewer::draw_keys()
{
  page.start(fmt);

  Page::Format kfmt = fmt;
  kfmt.font_size = FONT_SIZE;
  kfmt.align = CSS::Align::CENTER;

  Font * font = fonts.get(1);
  if (font == nullptr) return;

  for (uint8_t i = 0; i < key_count; i++) {
    page.put_rounded(keys[i].dim, keys[i].pos);
    const char * label = keys[i].label;
    char one[2];
    if ((label == nullptr) && (keys[i].code >= 0)) {
      one[0] = (char)keys[i].code;
      one[1] = 0;
      label = one;
    }
    if (label != nullptr) {
      Dim d;
      font->get_size(label, &d, FONT_SIZE);
      page.put_str_at(label,
                      Pos((int16_t)(keys[i].pos.x + (keys[i].dim.width >> 1) - (d.width >> 1)),
                          (int16_t)(keys[i].pos.y + (keys[i].dim.height >> 1) + (d.height >> 1))),
                      kfmt);
    }
  }
}

void
KeyboardViewer::show(char * str, uint16_t len, const char * _caption, bool is_password)
{
  client_buf = str;
  client_buf_len = len;
  caption = _caption;
  password_mode = is_password;

  if (client_buf == nullptr) return;
  if (client_buf_len == 0) return;

  client_len = (uint16_t)strlen(client_buf);
  if (client_len >= client_buf_len) client_len = (uint16_t)(client_buf_len - 1);
  clamp_client_len();

  (void)snprintf(original_buf, sizeof(original_buf), "%s", client_buf);

  fmt = {
    .line_height_factor = 1.0,
    .font_index         =   1,
    .font_size          = FONT_SIZE,
    .indent             =   0,
    .margin_left        =   0,
    .margin_right       =   0,
    .margin_top         =   0,
    .margin_bottom      =   0,
    .screen_left        =  20,
    .screen_right       =  20,
    .screen_top         =   0,
    .screen_bottom      =   0,
    .width              =   0,
    .height             =   0,
    .vertical_align     =   0,
    .trim               = true,
    .pre                = false,
    .font_style         = Fonts::FaceStyle::NORMAL,
    .align              = CSS::Align::CENTER,
    .text_transform     = CSS::TextTransform::NONE,
    .display            = CSS::Display::INLINE
  };

  modal_pos = Pos(18, 18);
  modal_dim = Dim((int16_t)(Screen::get_width() - 36), (int16_t)(Screen::get_height() - 36));

  value_pos = Pos((int16_t)(modal_pos.x + 12), (int16_t)(modal_pos.y + 46));
  value_dim = Dim((int16_t)(modal_dim.width - 24), 58);

  current_kb_type = KBType::ALFA;
  set_kb_type(current_kb_type);
  draw_static();
  draw_value();
  draw_keys();
  page.paint(false);
}

bool
KeyboardViewer::event(const EventMgr::Event & event)
{
  if (event.kind != EventMgr::EventKind::TAP) return true;

  const Key * k = find_key(event.x, event.y);
  if (k == nullptr) return true;

  const int16_t code = k->code;

  if (code >= 0) {
    append_char((char)code);
    draw_value();
    page.paint(false);
    return true;
  }

  switch ((KeyCode)code) {
    case KeyCode::SHIFT:
      set_kb_type((current_kb_type == KBType::ALFA_SHIFTED) ? KBType::ALFA : KBType::ALFA_SHIFTED);
      draw_static();
      draw_value();
      draw_keys();
      page.paint(false);
      return true;

    case KeyCode::BACKSPACE:
      backspace();
      draw_value();
      page.paint(false);
      return true;

    case KeyCode::SPACE:
      append_char(' ');
      draw_value();
      page.paint(false);
      return true;

    case KeyCode::MODE_ABC:
      set_kb_type(KBType::ALFA);
      draw_static();
      draw_value();
      draw_keys();
      page.paint(false);
      return true;

    case KeyCode::MODE_123:
      set_kb_type(KBType::NUMBERS);
      draw_static();
      draw_value();
      draw_keys();
      page.paint(false);
      return true;

    case KeyCode::MODE_SPECIAL:
      set_kb_type(KBType::SPECIAL);
      draw_static();
      draw_value();
      draw_keys();
      page.paint(false);
      return true;

    case KeyCode::OK:
      return false;

    case KeyCode::CANCEL:
      if (client_buf != nullptr) {
        (void)snprintf(client_buf, client_buf_len, "%s", original_buf);
        client_len = (uint16_t)strlen(client_buf);
        clamp_client_len();
      }
      return false;
  }

  return true;
}

#endif