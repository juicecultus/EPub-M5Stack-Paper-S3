// Copyright (c) 2020 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.

#define __MENU_VIEWER__ 1
#include "viewers/menu_viewer.hpp"

#include "viewers/book_viewer.hpp"
#include "models/fonts.hpp"
#include "viewers/page.hpp"
#include "viewers/screen_bottom.hpp"
#include "screen.hpp"
#include "controllers/app_controller.hpp"
#if EPUB_INKPLATE_BUILD
  #include "esp.hpp"
#endif

#include <string>
#include <vector>
#include <algorithm>

static const std::string TOUCH_AND_HOLD_STR = "Touch and hold icon for info. Tap for action.";
static const char * MENU_TOUCH_AND_HOLD_STR = "Touch and hold the menu icon for info. Tap to open.";

#if defined(BOARD_TYPE_PAPER_S3)
static constexpr uint16_t INVALID_POS = 0xFFFF;
#endif

#if defined(BOARD_TYPE_PAPER_S3)
static inline int16_t
paper_s3_menu_footer_h()
{
  // Match the matrix view header height used above the cover grid.
  // See MatrixBooksDirViewer::setup() for the reference computation.
  static constexpr int16_t TITLE_FONT_SIZE   =  8;
  static constexpr int16_t AUTHOR_FONT_SIZE  =  6;
  static constexpr int16_t SPACE_BELOW_INFO  = 10;
  static constexpr int16_t DIVIDER_OFFSET_Y  =  8;
  static constexpr int16_t TOP_MARGIN        = 10;

  Font * title_font  = fonts.get(1);
  Font * author_font = fonts.get(2);
  if (title_font == nullptr || author_font == nullptr) return 70;

  // Matrix uses: line_height * 0.8
  const int16_t title_h  = (int16_t)((title_font->get_line_height(TITLE_FONT_SIZE)  * 4) / 5);
  const int16_t author_h = (int16_t)((author_font->get_line_height(AUTHOR_FONT_SIZE) * 4) / 5);
  const int16_t first_entry_ypos = (title_h << 1) + author_h + SPACE_BELOW_INFO + TOP_MARGIN;
  const int16_t header_h = first_entry_ypos - DIVIDER_OFFSET_Y;

  return (header_h > 0) ? header_h : 70;
}

static const char *
short_caption(MenuViewer::Icon icon)
{
  switch (icon) {
    case MenuViewer::Icon::RETURN:      return "Back";
    case MenuViewer::Icon::BOOK:        return "Last Book";
    case MenuViewer::Icon::BOOK_LIST:   return "Library";
    case MenuViewer::Icon::MAIN_PARAMS: return "Settings";
    case MenuViewer::Icon::FONT_PARAMS: return "Text";
    case MenuViewer::Icon::TOC:         return "Contents";
    case MenuViewer::Icon::INFO:        return "About";
    case MenuViewer::Icon::WIFI:        return "WiFi";
    case MenuViewer::Icon::REFRESH:     return "Refresh";
    case MenuViewer::Icon::CLR_HISTORY: return "History";
    case MenuViewer::Icon::DELETE:      return "Delete";
    case MenuViewer::Icon::REVERT:      return "Revert";
    case MenuViewer::Icon::CLOCK:       return "Clock";
    case MenuViewer::Icon::NTP_CLOCK:   return "NTP";
    case MenuViewer::Icon::CALIB:       return "Calibrate";
    case MenuViewer::Icon::POWEROFF:    return "Power";
    case MenuViewer::Icon::DEBUG:       return "Debug";
    case MenuViewer::Icon::PREV_MENU:   return "Prev";
    case MenuViewer::Icon::NEXT_MENU:   return "Next";
    case MenuViewer::Icon::END_MENU:    return "";
  }
  return "";
}

static inline Pos
center_glyph_in_box(Font & icon_font, char ch, int16_t icon_size, Pos box_pos, Dim box_dim)
{
  Font::Glyph * g = icon_font.get_glyph(ch, icon_size);
  if (g == nullptr) return Pos(box_pos.x, box_pos.y);

  const int16_t left = box_pos.x + (box_dim.width  - g->dim.width)  / 2;
  const int16_t top  = box_pos.y + (box_dim.height - g->dim.height) / 2;
  return Pos(left - g->xoff, top - g->yoff);
}

static std::string
truncate_to_width(Font & font, const char * txt, int16_t max_w, int8_t font_size)
{
  if (txt == nullptr) return std::string();
  std::string s(txt);
  Dim dim;
  font.get_size(s.c_str(), &dim, font_size);
  if (dim.width <= max_w) return s;

  static constexpr const char * ell = "...";
  std::string base(txt);

  while (base.size() > 0) {
    std::string candidate = base;
    if (candidate.size() > 1) {
      candidate.resize(candidate.size() - 1);
    }
    candidate.append(ell);
    font.get_size(candidate.c_str(), &dim, font_size);
    if (dim.width <= max_w) return candidate;
    base.resize(base.size() - 1);
  }
  return std::string(ell);
}

static std::string
ellipsize_to_width(Font & font, const std::string & txt, int16_t max_w, int8_t font_size)
{
  Dim dim;
  font.get_size(txt.c_str(), &dim, font_size);
  if (dim.width <= max_w) return txt;

  static constexpr const char * ell = "...";
  std::string base = txt;

  while (base.size() > 0) {
    std::string candidate = base;
    candidate.append(ell);
    font.get_size(candidate.c_str(), &dim, font_size);
    if (dim.width <= max_w) return candidate;
    base.resize(base.size() - 1);
  }

  return std::string(ell);
}

static void
wrap_two_lines(Font & font, const char * txt, int16_t max_w, int8_t font_size, std::string & line1, std::string & line2)
{
  line1.clear();
  line2.clear();
  if (txt == nullptr) return;

  std::string s(txt);
  for (char & c : s) {
    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
  }

  auto fits = [&](const std::string & candidate) {
    Dim dim;
    font.get_size(candidate.c_str(), &dim, font_size);
    return dim.width <= max_w;
  };

  bool truncated = false;

  size_t i = 0;
  while (i < s.size()) {
    while ((i < s.size()) && (s[i] == ' ')) i++;
    if (i >= s.size()) break;

    size_t j = i;
    while ((j < s.size()) && (s[j] != ' ')) j++;
    const std::string word = s.substr(i, j - i);
    i = j;

    if (line2.size() > 0 && truncated) break;

    if (line2.empty()) {
      std::string candidate = line1;
      if (!candidate.empty()) candidate.push_back(' ');
      candidate.append(word);

      if (fits(candidate)) {
        line1 = candidate;
        continue;
      }

      if (line1.empty()) {
        line1 = ellipsize_to_width(font, word, max_w, font_size);
        truncated = true;
        break;
      }

      line2 = word;
      if (!fits(line2)) {
        line2 = ellipsize_to_width(font, line2, max_w, font_size);
        truncated = true;
        break;
      }
      continue;
    }

    std::string candidate = line2;
    if (!candidate.empty()) candidate.push_back(' ');
    candidate.append(word);

    if (fits(candidate)) {
      line2 = candidate;
      continue;
    }

    truncated = true;
    break;
  }

  if (truncated && !line2.empty()) {
    line2 = ellipsize_to_width(font, line2, max_w, font_size);
  }
}
#endif

void MenuViewer::show(MenuEntry * the_menu, uint8_t entry_index, bool clear_screen)
{
#if defined(BOARD_TYPE_PAPER_S3)
  Font * caption_font = fonts.get(1);
  Font * icon_font    = fonts.get(0);

  if (caption_font == nullptr || icon_font == nullptr) {
    LOG_E("Internal error (Fonts not available!)");
    return;
  }

  const bool menu_changed = (menu != the_menu);
  menu = the_menu;

  const int16_t screen_w = Screen::get_width();
  const int16_t screen_h = Screen::get_height();

  const int16_t margin_x  = 18;
  const int16_t margin_y  = 16;
  const int16_t gap       = 14;
  const int16_t footer_h  = paper_s3_menu_footer_h();
  const int16_t icon_size = 26;
  const int16_t label_size = 12;
  const int16_t hint_size  = 8;

  const int16_t cols = (screen_w >= 520) ? 3 : 2;
  const int16_t tile_w = (screen_w - (2 * margin_x) - ((cols - 1) * gap)) / cols;
  const int16_t tile_h = 124;

  const int16_t avail_h = screen_h - footer_h - (2 * margin_y);
  const int16_t rows = (tile_h + gap) > 0 ? (int16_t)std::max<int>(1, (avail_h + gap) / (tile_h + gap)) : 1;

  uint8_t return_idx = 0xFF;
  std::vector<uint8_t> actions;
  actions.reserve(MAX_MENU_ENTRY);

  for (uint8_t i = 0; i < MAX_MENU_ENTRY; i++) {
    entry_locs[i].pos = Pos(INVALID_POS, INVALID_POS);
    entry_locs[i].dim = Dim(0, 0);
  }

  for (uint8_t i = 0; (i < MAX_MENU_ENTRY) && (menu[i].icon != Icon::END_MENU); i++) {
    if (!menu[i].visible) {
      entry_locs[i].pos = Pos(INVALID_POS, INVALID_POS);
      entry_locs[i].dim = Dim(0, 0);
      continue;
    }
    if (menu[i].icon == Icon::RETURN && return_idx == 0xFF) {
      return_idx = i;
    }
    else {
      actions.push_back(i);
    }
  }

  const int16_t reserved_slots = (return_idx == 0xFF) ? 0 : 1;
  int16_t tiles_per_page = cols * rows - reserved_slots;
  if (tiles_per_page < 1) tiles_per_page = 1;

  if (tiles_per_page <= 0) {
    page_count = 1;
  }
  else {
    page_count = (uint8_t)((actions.size() + tiles_per_page - 1) / tiles_per_page);
    if (page_count == 0) page_count = 1;
  }

  if (menu_changed) {
    page_index = 0;
  }

  if (entry_index < MAX_MENU_ENTRY) {
    for (size_t i = 0; i < actions.size(); i++) {
      if (actions[i] == entry_index) {
        page_index = (uint8_t)(i / tiles_per_page);
        break;
      }
    }
  }
  if (page_index >= page_count) page_index = 0;

  Page::Format fmt_icon = {
    .line_height_factor =   1.0,
    .font_index         =     0,
    .font_size          = (int8_t)icon_size,
    .indent             =     0,
    .margin_left        =     0,
    .margin_right       =     0,
    .margin_top         =     0,
    .margin_bottom      =     0,
    .screen_left        =     0,
    .screen_right       =     0,
    .screen_top         =     0,
    .screen_bottom      =     0,
    .width              =     0,
    .height             =     0,
    .vertical_align     =     0,
    .trim               =  true,
    .pre                = false,
    .font_style         = Fonts::FaceStyle::NORMAL,
    .align              = CSS::Align::LEFT,
    .text_transform     = CSS::TextTransform::NONE,
    .display            = CSS::Display::INLINE
  };

  Page::Format fmt_label = fmt_icon;
  fmt_label.font_index = 1;
  fmt_label.font_size  = (int8_t)label_size;

  Page::Format fmt_hint = fmt_label;
  fmt_hint.font_size = (int8_t)hint_size;

  page.start(fmt_icon);

  page.clear_region(Dim{ (uint16_t)screen_w, (uint16_t)screen_h }, Pos{ 0, 0 });

  auto draw_tile = [&](uint8_t menu_idx, const Pos & tile_pos, const Dim & tile_dim) {
    entry_locs[menu_idx].pos = tile_pos;
    entry_locs[menu_idx].dim = tile_dim;

    page.put_rounded(tile_dim, tile_pos);

    const char ch = icon_char[(int)menu[menu_idx].icon];
    const Dim icon_box(tile_dim.width, 66);
    const Pos icon_box_pos(tile_pos.x, tile_pos.y + 10);
    const Pos icon_pos = center_glyph_in_box(*icon_font, ch, icon_size, icon_box_pos, icon_box);
    page.put_char_at(ch, icon_pos, fmt_icon);

    const char * short_txt = short_caption(menu[menu_idx].icon);
    std::string label = truncate_to_width(*caption_font, short_txt, tile_dim.width - 16, (int8_t)label_size);
    Dim label_dim;
    caption_font->get_size(label.c_str(), &label_dim, (int8_t)label_size);
    int16_t lx = tile_pos.x;
    if (label_dim.width < tile_dim.width) {
      lx = tile_pos.x + (tile_dim.width - label_dim.width) / 2;
    }
    page.put_str_at(label, Pos(lx, tile_pos.y + tile_dim.height - 18), fmt_label);
  };

  // Back tile (fixed position, always visible)
  const Dim tile_dim(tile_w, tile_h);
  if (return_idx != 0xFF) {
    const Pos back_pos(margin_x, margin_y);
    draw_tile(return_idx, back_pos, tile_dim);
  }

  // Footer hint area
  hint_shown = false;
  const int16_t footer_top = screen_h - footer_h;
  page.put_highlight(Dim(screen_w - 20, 3), Pos(10, footer_top));
  {
    const int16_t text_left = margin_x;
    const int16_t max_w = (int16_t)(screen_w - (2 * text_left));
    std::string line1;
    std::string line2;
    wrap_two_lines(*caption_font, MENU_TOUCH_AND_HOLD_STR, max_w, (int8_t)hint_size, line1, line2);

    const int16_t border_h = 3;
    const int16_t pad_top  = 6;
    const int16_t ascent   = (int16_t)caption_font->get_chars_height(hint_size);
    int16_t line_step      = (int16_t)caption_font->get_line_height(hint_size);
    if (line_step <= 0) line_step = 14;

    const int16_t y1 = footer_top + border_h + pad_top + ascent;
    if (!line1.empty()) page.put_str_at(line1, Pos(text_left, y1), fmt_hint);
    if (!line2.empty()) page.put_str_at(line2, Pos(text_left, y1 + line_step), fmt_hint);
  }

  // Tiles
  const size_t start = (size_t)page_index * (size_t)tiles_per_page;
  const size_t end   = std::min(actions.size(), start + (size_t)tiles_per_page);

  for (size_t i = start; i < end; i++) {
    const uint8_t menu_idx = actions[i];
    const int16_t local = (int16_t)(i - start);
    const int16_t slot = local + reserved_slots;
    const int16_t col = (int16_t)(slot % cols);
    const int16_t row = (int16_t)(slot / cols);

    const int16_t x = margin_x + col * (tile_w + gap);
    const int16_t y = margin_y + row * (tile_h + gap);
    const Pos tile_pos(x, y);

    draw_tile(menu_idx, tile_pos, tile_dim);
  }

  ScreenBottom::show();
  page.paint(true);
  return;
#endif

  Font * font = fonts.get(1);

  if (font == nullptr) {
    LOG_E("Internal error (Main Font not available!");
    return;
  }

  line_height = font->get_line_height(CAPTION_SIZE);
  text_height = line_height - font->get_descender_height(CAPTION_SIZE); 

  font = fonts.get(0);

  if (font == nullptr) {
    LOG_E("Internal error (Drawings Font not available!");
    return;
  }

  Font::Glyph * icon = font->get_glyph('A', ICON_SIZE);

  if (icon == nullptr) {
    icon_height   = 50;
    icon_ypos     = 10 + icon_height;
    text_ypos     = icon_ypos + line_height + 10;
  }
  else {
    icon_height   = icon->dim.height;
    icon_ypos     = 10 + icon_height;
    text_ypos     = icon_ypos + line_height + 10;
  }

  region_height = text_ypos + 20;

  Page::Format fmt = {
    .line_height_factor =   1.0,
    .font_index         =     0,
    .font_size          = ICON_SIZE,
    .indent             =     0,
    .margin_left        =     0,
    .margin_right       =     0,
    .margin_top         =     0,
    .margin_bottom      =     0,
    .screen_left        =    10,
    .screen_right       =    10,
    .screen_top         =    10,
    .screen_bottom      =   100,
    .width              =     0,
    .height             =     0,
    .vertical_align     =     0,
    .trim               =  true,
    .pre                = false,
    .font_style         = Fonts::FaceStyle::NORMAL,
    .align              = CSS::Align::LEFT,
    .text_transform     = CSS::TextTransform::NONE,
    .display            = CSS::Display::INLINE
  };

  page.start(fmt);

  page.clear_region(Dim{ Screen::get_width(), region_height }, Pos{ 0, 0 });

  menu = the_menu;

  uint8_t idx = 0;

  Pos pos(ICONS_LEFT_OFFSET, icon_ypos);
  
  while ((idx < MAX_MENU_ENTRY) && (menu[idx].icon != Icon::END_MENU)) {

    if (menu[idx].visible) {
      char ch = icon_char[(int)menu[idx].icon];
      Font::Glyph * glyph;
      glyph = font->get_glyph(ch, ICON_SIZE);

      if (menu[idx].icon == Icon::NEXT_MENU) pos.x = Screen::get_width() - SPACE_BETWEEN_ICONS;

      if (glyph == nullptr) {
        entry_locs[idx].pos = pos;
        entry_locs[idx].dim = Dim(0, 0);
      }
      else {
        entry_locs[idx].pos.x = pos.x;
        entry_locs[idx].pos.y = pos.y + glyph->yoff;
        entry_locs[idx].dim   = glyph->dim;
      }
      // page.put_highlight(
      //   Dim(entry_locs[idx].dim.width + 30, entry_locs[idx].pos.y + entry_locs[idx].dim.height + 15), 
      //   Pos(entry_locs[idx].pos.x - 15, 0));

      page.put_char_at(ch, pos, fmt);
      pos.x += SPACE_BETWEEN_ICONS;

      // std::cout << "[" 
      //           << entry_locs[idx].pos.x 
      //           << ", " 
      //           << entry_locs[idx].pos.y
      //           << ":"
      //           << entry_locs[idx].dim.width
      //           << ", "
      //           << entry_locs[idx].dim.height
      //           << "] ";
    }
    else {
      entry_locs[idx].pos.x = -1;
      entry_locs[idx].pos.y = -1;
    }

    idx++;
  }

  // std::cout << std::endl;
  
  max_index           = idx - 1;
  // It is expected that the last entry in the menu will be always visible
  // If not, shit happen...
  while (!menu[entry_index].visible) entry_index++;
  current_entry_index = entry_index;

  #if !(INKPLATE_6PLUS || TOUCH_TRIAL)
    page.put_highlight(
      Dim(entry_locs[entry_index].dim.width  + 8, entry_locs[entry_index].dim.height + 8), 
      Pos(entry_locs[entry_index].pos.x      - 4, entry_locs[entry_index].pos.y - 4));
  #endif

  fmt.font_index = 1;
  fmt.font_size  = CAPTION_SIZE;
  
  #if (INKPLATE_6PLUS || TOUCH_TRIAL)
    page.put_str_at(TOUCH_AND_HOLD_STR, Pos{ 10, text_ypos }, fmt);
    hint_shown = false;
  #else
    std::string txt = menu[entry_index].caption; 
    page.put_str_at(txt, Pos{ 10, text_ypos }, fmt);
  #endif

  page.put_highlight(
    Dim(Screen::get_width() - 20, 3), 
    Pos(10, region_height - 12));

  ScreenBottom::show();

  page.paint(clear_screen);
}

#if (INKPLATE_6PLUS || TOUCH_TRIAL || defined(BOARD_TYPE_PAPER_S3))
  uint8_t
  MenuViewer::find_index(uint16_t x, uint16_t y)
  {
    LOG_D("Find Index: [%u %u]", x, y);
    
    // page.put_highlight(Dim(5, 5), Pos(x-2, y-2));
    // page.put_highlight(Dim(7, 7), Pos(x-3, y-3));
    // page.paint(false, true, true);

    #if defined(BOARD_TYPE_PAPER_S3)
      for (uint8_t idx = 0; idx < MAX_MENU_ENTRY; idx++) {
        if (entry_locs[idx].pos.x == INVALID_POS) continue;
        if ((x >= (uint16_t)entry_locs[idx].pos.x) &&
            (y >= (uint16_t)entry_locs[idx].pos.y) &&
            (x <= (uint16_t)(entry_locs[idx].pos.x + entry_locs[idx].dim.width)) &&
            (y <= (uint16_t)(entry_locs[idx].pos.y + entry_locs[idx].dim.height))) {
          return idx;
        }
      }

      return MAX_MENU_ENTRY;
    #else
      for (int8_t idx = 0; idx <= max_index; idx++) {
        if ((x >=  entry_locs[idx].pos.x - 15) &&
            (x <= (entry_locs[idx].pos.x + entry_locs[idx].dim.width + 15)) &&
            //(y >=  0) &&
            (y <= (entry_locs[idx].pos.y + entry_locs[idx].dim.height + 15))) {
          return idx;
        }
      }

      return max_index + 1;
    #endif
  }
#endif

void 
MenuViewer::clear_highlight()
{
  #if (INKPLATE_6PLUS || TOUCH_TRIAL || defined(BOARD_TYPE_PAPER_S3))
    Page::Format fmt = {
      .line_height_factor =   1.0,
      .font_index         =     1,
      .font_size          = CAPTION_SIZE,
      .indent             =     0,
      .margin_left        =     0,
      .margin_right       =     0,
      .margin_top         =     0,
      .margin_bottom      =     0,
      .screen_left        =    10,
      .screen_right       =    10,
      .screen_top         =    10,
      .screen_bottom      =     0,
      .width              =     0,
      .height             =     0,
      .vertical_align     =     0,
      .trim               =  true,
      .pre                = false,
      .font_style         = Fonts::FaceStyle::NORMAL,
      .align              = CSS::Align::LEFT,
      .text_transform     = CSS::TextTransform::NONE,
      .display            = CSS::Display::INLINE
    };

    page.start(fmt);

#if defined(BOARD_TYPE_PAPER_S3)
    if (hint_shown) {
      hint_shown = false;
      const int16_t footer_h = paper_s3_menu_footer_h();
      const int16_t footer_top = Screen::get_height() - footer_h;
      page.clear_region(Dim(Screen::get_width(), footer_h), Pos(0, footer_top));
      page.put_highlight(Dim(Screen::get_width() - 20, 3), Pos(10, footer_top));

      Font * caption_font = fonts.get(1);
      if (caption_font != nullptr) {
        static constexpr int16_t hint_size = 8;
        Page::Format hint_fmt = {
          .line_height_factor = 1.0,
          .font_index         = 1,
          .font_size          = hint_size,
          .indent             = 0,
          .margin_left        = 0,
          .margin_right       = 0,
          .margin_top         = 0,
          .margin_bottom      = 0,
          .screen_left        = 0,
          .screen_right       = 0,
          .screen_top         = 0,
          .screen_bottom      = 0,
          .width              = 0,
          .height             = 0,
          .vertical_align     = 0,
          .trim               = true,
          .pre                = false,
          .font_style         = Fonts::FaceStyle::NORMAL,
          .align              = CSS::Align::LEFT,
          .text_transform     = CSS::TextTransform::NONE,
          .display            = CSS::Display::INLINE
        };

        const int16_t text_left = 18;
        const int16_t max_w = (int16_t)(Screen::get_width() - (2 * text_left));
        std::string line1;
        std::string line2;
        wrap_two_lines(*caption_font, MENU_TOUCH_AND_HOLD_STR, max_w, (int8_t)hint_size, line1, line2);

        const int16_t border_h = 3;
        const int16_t pad_top  = 6;
        const int16_t ascent   = (int16_t)caption_font->get_chars_height(hint_size);
        int16_t line_step      = (int16_t)caption_font->get_line_height(hint_size);
        if (line_step <= 0) line_step = 14;

        const int16_t y1 = footer_top + border_h + pad_top + ascent;
        if (!line1.empty()) page.put_str_at(line1, Pos(text_left, y1), hint_fmt);
        if (!line2.empty()) page.put_str_at(line2, Pos(text_left, y1 + line_step), hint_fmt);
      }
    }

    page.paint(false);
    return;
#endif

    if (hint_shown) {
      hint_shown     = false;

      page.clear_highlight(
        Dim(entry_locs[current_entry_index].dim.width + 8, entry_locs[current_entry_index].dim.height + 8), 
        Pos(entry_locs[current_entry_index].pos.x - 4,     entry_locs[current_entry_index].pos.y - 4     ));

      page.clear_region(Dim(Screen::get_width(), text_height), Pos(0, text_ypos - line_height));
      page.put_str_at(TOUCH_AND_HOLD_STR, Pos{ 10, text_ypos }, fmt);
    }

    page.paint(false);
  #endif
}

bool 
MenuViewer::event(const EventMgr::Event & event)
{
#if defined(BOARD_TYPE_PAPER_S3)
  if (menu == nullptr) return false;

  Font * caption_font = fonts.get(1);
  if (caption_font == nullptr) return false;

  const int16_t hint_size = 8;

  Page::Format fmt = {
    .line_height_factor =   1.0,
    .font_index         =     1,
    .font_size          = hint_size,
    .indent             =     0,
    .margin_left        =     0,
    .margin_right       =     0,
    .margin_top         =     0,
    .margin_bottom      =     0,
    .screen_left        =     0,
    .screen_right       =     0,
    .screen_top         =     0,
    .screen_bottom      =     0,
    .width              =     0,
    .height             =     0,
    .vertical_align     =     0,
    .trim               =  true,
    .pre                = false,
    .font_style         = Fonts::FaceStyle::NORMAL,
    .align              = CSS::Align::LEFT,
    .text_transform     = CSS::TextTransform::NONE,
    .display            = CSS::Display::INLINE
  };

  const int16_t footer_h = paper_s3_menu_footer_h();
  const int16_t footer_top = Screen::get_height() - footer_h;

  switch (event.kind) {
    case EventMgr::EventKind::SWIPE_LEFT:
      if (page_count > 1) {
        page_index = (uint8_t)((page_index + 1) % page_count);
        show(menu, MAX_MENU_ENTRY, true);
      }
      return false;

    case EventMgr::EventKind::SWIPE_RIGHT:
      if (page_count > 1) {
        page_index = (uint8_t)((page_index + page_count - 1) % page_count);
        show(menu, MAX_MENU_ENTRY, true);
      }
      return false;

    case EventMgr::EventKind::HOLD: {
      const uint8_t idx = find_index(event.x, event.y);
      if (idx < MAX_MENU_ENTRY && entry_locs[idx].pos.x != INVALID_POS && menu[idx].visible) {
        page.start(fmt);
        page.clear_region(Dim(Screen::get_width(), footer_h), Pos(0, footer_top));
        page.put_highlight(Dim(Screen::get_width() - 20, 3), Pos(10, footer_top));
        if (menu[idx].caption != nullptr) {
          const int16_t text_left = 18;
          const int16_t max_w = (int16_t)(Screen::get_width() - (2 * text_left));
          std::string line1;
          std::string line2;
          wrap_two_lines(*caption_font, menu[idx].caption, max_w, (int8_t)fmt.font_size, line1, line2);

          const int16_t border_h = 3;
          const int16_t pad_top  = 6;
          const int16_t ascent   = (int16_t)caption_font->get_chars_height((int16_t)fmt.font_size);
          int16_t line_step      = (int16_t)caption_font->get_line_height((int16_t)fmt.font_size);
          if (line_step <= 0) line_step = 18;

          const int16_t y1 = footer_top + border_h + pad_top + ascent;
          if (!line1.empty()) page.put_str_at(line1, Pos(text_left, y1), fmt);
          if (!line2.empty()) page.put_str_at(line2, Pos(text_left, y1 + line_step), fmt);
        }
        hint_shown = true;
        page.paint(false);
      }
      return false;
    }

    case EventMgr::EventKind::RELEASE:
      clear_highlight();
      return false;

    case EventMgr::EventKind::TAP: {
      const uint8_t idx = find_index(event.x, event.y);
      if (idx < MAX_MENU_ENTRY && entry_locs[idx].pos.x != INVALID_POS && menu[idx].visible) {
        if (menu[idx].func != nullptr) {
          (*menu[idx].func)();
        }
      }
      return false;
    }

    default:
      break;
  }

  return false;
#else

  Page::Format fmt = {
    .line_height_factor =   1.0,
    .font_index         =     1,
    .font_size          = CAPTION_SIZE,
    .indent             =     0,
    .margin_left        =     0,
    .margin_right       =     0,
    .margin_top         =     0,
    .margin_bottom      =     0,
    .screen_left        =    10,
    .screen_right       =    10,
    .screen_top         =    10,
    .screen_bottom      =     0,
    .width              =     0,
    .height             =     0,
    .vertical_align     =     0,
    .trim               =  true,
    .pre                = false,
    .font_style         = Fonts::FaceStyle::NORMAL,
    .align              = CSS::Align::LEFT,
    .text_transform     = CSS::TextTransform::NONE,
    .display            = CSS::Display::INLINE
  };

  #if (INKPLATE_6PLUS || TOUCH_TRIAL)

    switch (event.kind) {
      case EventMgr::EventKind::HOLD:
        current_entry_index = find_index(event.x, event.y);
        if (current_entry_index <= max_index) {
          page.start(fmt);

          fmt.font_index =  1;
          fmt.font_size  = CAPTION_SIZE;
        
          page.clear_region(Dim(Screen::get_width(), text_height), Pos(0, text_ypos - line_height));

          std::string txt = menu[current_entry_index].caption; 
          page.put_str_at(txt, Pos{ 10, text_ypos }, fmt);
          hint_shown = true;

          page.paint(false);
        }
        break;

      case EventMgr::EventKind::RELEASE:
        #if EPUB_INKPLATE_BUILD
          ESP::delay(1000);
        #endif
        clear_highlight();
        hint_shown = false;
        break;

      case EventMgr::EventKind::TAP:
        current_entry_index = find_index(event.x, event.y);
        if (current_entry_index <= max_index) {
          if (menu[current_entry_index].func != nullptr) {
            if (menu[current_entry_index].highlight) {
              page.start(fmt);

              fmt.font_index = 1;
              fmt.font_size  = CAPTION_SIZE;
            
              page.clear_region(Dim(Screen::get_width(), text_height), Pos(0, text_ypos - line_height));

              std::string txt = menu[current_entry_index].caption; 
              page.put_str_at(txt, Pos{ 10, text_ypos }, fmt);
              hint_shown = true;

              page.put_highlight(
                Dim(entry_locs[current_entry_index].dim.width + 8, entry_locs[current_entry_index].dim.height + 8),
                Pos(entry_locs[current_entry_index].pos.x - 4,     entry_locs[current_entry_index].pos.y - 4     ));

              page.paint(false);
            }
            else {
              hint_shown = false;
            }

            (*menu[current_entry_index].func)();
          }
          return false;
        }
        break;

      default:
        break;
    }
  #else
    uint8_t old_index = current_entry_index;

    page.start(fmt);

    switch (event.kind) {
      case EventMgr::EventKind::PREV:
        if (current_entry_index > 0) {
          current_entry_index--;
          // It is expected that the first entry in the menu will always be visible
          while (!menu[current_entry_index].visible) current_entry_index--;
        }
        else {
          current_entry_index = max_index;
        }
        break;
      case EventMgr::EventKind::NEXT:
        if (current_entry_index < max_index) {
          current_entry_index++;
          // It is expected that the last entry in the menu will always be visible
          while (!menu[current_entry_index].visible) current_entry_index++;
        }
        else {
          current_entry_index = 0;
        }
        break;
      case EventMgr::EventKind::DBL_PREV:
        return false;
      case EventMgr::EventKind::DBL_NEXT:
        return false;
      case EventMgr::EventKind::SELECT:
        if (menu[current_entry_index].func != nullptr) (*menu[current_entry_index].func)();
        return false;
      case EventMgr::EventKind::DBL_SELECT:
        return true;
      case EventMgr::EventKind::NONE:
        return false;
    }

    if (current_entry_index != old_index) {
      page.clear_highlight(
        Dim(entry_locs[old_index].dim.width + 8, entry_locs[old_index].dim.height + 8), 
        Pos(entry_locs[old_index].pos.x - 4,     entry_locs[old_index].pos.y - 4     ));
        
      page.put_highlight(
        Dim(entry_locs[current_entry_index].dim.width  + 8, entry_locs[current_entry_index].dim.height + 8),
        Pos(entry_locs[current_entry_index].pos.x - 4,      entry_locs[current_entry_index].pos.y - 4     ));

      fmt.font_index = 1;
      fmt.font_size  = CAPTION_SIZE;
    
      page.clear_region(Dim(Screen::get_width(), text_height), Pos(0, text_ypos - line_height));

      std::string txt = menu[current_entry_index].caption; 
      page.put_str_at(txt, Pos{ 10, text_ypos }, fmt);
    }

    ScreenBottom::show();

    page.paint(false);
  #endif
  
  return false;
  #endif
}
