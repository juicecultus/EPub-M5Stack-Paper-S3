// Copyright (c) 2020 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.

#define __LINEAR_BOOKS_DIR_VIEWER__ 1
#include "viewers/linear_books_dir_viewer.hpp"

#include "models/fonts.hpp"
#include "models/config.hpp"
#include "viewers/page.hpp"
#include "viewers/screen_bottom.hpp"

#if EPUB_INKPLATE_BUILD
  #include "viewers/battery_viewer.hpp"
  #include "models/nvs_mgr.hpp"
#endif

#include "screen.hpp"

#include <iomanip>
#include <vector>

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

static std::vector<std::string>
wrap_to_width(Font & font, const char * txt, int16_t max_w, int8_t font_size)
{
  std::vector<std::string> lines;
  if (txt == nullptr) return lines;

  std::string s(txt);
  if (s.empty()) return lines;

  size_t pos = 0;
  while (pos < s.size()) {
    while ((pos < s.size()) && (s[pos] == ' ')) pos++;
    if (pos >= s.size()) break;

    size_t next_space = s.find(' ', pos);
    std::string word = (next_space == std::string::npos) ? s.substr(pos) : s.substr(pos, next_space - pos);
    pos = (next_space == std::string::npos) ? s.size() : (next_space + 1);

    if (lines.empty()) lines.emplace_back();
    std::string & current = lines.back();

    auto fits = [&](const std::string & candidate) {
      Dim dim;
      font.get_size(candidate.c_str(), &dim, font_size);
      return dim.width <= max_w;
    };

    if (current.empty()) {
      if (fits(word)) {
        current = word;
      }
      else {
        std::string rest = word;
        while (!rest.empty()) {
          size_t n = rest.size();
          std::string part;
          while (n > 0) {
            part = rest.substr(0, n);
            if (fits(part)) break;
            n--;
          }
          if (n == 0) {
            part = rest.substr(0, 1);
            n = 1;
          }
          current = part;
          rest.erase(0, n);
          if (!rest.empty()) {
            lines.emplace_back();
            current = lines.back();
          }
        }
      }
    }
    else {
      std::string candidate = current;
      candidate.push_back(' ');
      candidate.append(word);
      if (fits(candidate)) {
        current = candidate;
      }
      else {
        lines.emplace_back(word);
        if (!fits(lines.back())) {
          std::string rest = lines.back();
          lines.back().clear();
          while (!rest.empty()) {
            size_t n = rest.size();
            std::string part;
            while (n > 0) {
              part = rest.substr(0, n);
              if (fits(part)) break;
              n--;
            }
            if (n == 0) {
              part = rest.substr(0, 1);
              n = 1;
            }
            lines.back() = part;
            rest.erase(0, n);
            if (!rest.empty()) {
              lines.emplace_back();
            }
          }
        }
      }
    }
  }

  return lines;
}

void
LinearBooksDirViewer::setup()
{
  #if defined(BOARD_TYPE_PAPER_S3)
    books_per_page = 4;

    Font * bottom_font = fonts.get(ScreenBottom::FONT);
    int16_t bottom_h = 20;
    if (bottom_font != nullptr) {
      bottom_h = (int16_t)bottom_font->get_chars_height(ScreenBottom::FONT_SIZE) + 10;
    }
    int16_t usable_h = (int16_t)(Screen::get_height() - bottom_h - FIRST_ENTRY_YPOS);
    if (usable_h < 1) usable_h = 1;
    row_height = (int16_t)(usable_h / books_per_page);
    if (row_height < (SPACE_BETWEEN_ENTRIES + 1)) {
      row_height = (int16_t)(BooksDir::max_cover_height + SPACE_BETWEEN_ENTRIES);
    }
  #else
    books_per_page = (Screen::get_height() - FIRST_ENTRY_YPOS - 20 + SPACE_BETWEEN_ENTRIES) / 
                     (BooksDir::max_cover_height + SPACE_BETWEEN_ENTRIES);
  #endif
  page_count = (books_dir.get_book_count() + books_per_page - 1) / books_per_page;

  current_page_nbr = -1;
  current_book_idx = -1;
  current_item_idx = -1;

  LOG_D("Books count: %d", books_dir.get_book_count());
}

void 
LinearBooksDirViewer::show_page(int16_t page_nbr, int16_t hightlight_item_idx)
{
  current_page_nbr = page_nbr;
  current_item_idx = hightlight_item_idx;

  int16_t book_idx = page_nbr * books_per_page;
  int16_t last     = book_idx + books_per_page;

  page.set_compute_mode(Page::ComputeMode::DISPLAY);

  if (last > books_dir.get_book_count()) last = books_dir.get_book_count();

  #if defined(BOARD_TYPE_PAPER_S3)
    const int16_t cover_box_h = (int16_t)(((row_height > 0) ? row_height : (BooksDir::max_cover_height + SPACE_BETWEEN_ENTRIES)) - SPACE_BETWEEN_ENTRIES);
    int16_t cover_box_w = (int16_t)((int32_t)cover_box_h * (int32_t)BooksDir::max_cover_width / (int32_t)BooksDir::max_cover_height);
    if (cover_box_w < 1) cover_box_w = BooksDir::max_cover_width;
  #else
    const int16_t cover_box_w = BooksDir::max_cover_width;
    const int16_t cover_box_h = BooksDir::max_cover_height;
  #endif

  const int16_t row_stride = (int16_t)(cover_box_h + SPACE_BETWEEN_ENTRIES);

  #if defined(BOARD_TYPE_PAPER_S3)
    row_height = row_stride;
  #endif

  int16_t xpos = 20 + cover_box_w;
  int16_t ypos = FIRST_ENTRY_YPOS;

  Page::Format fmt = {
      .line_height_factor =   0.8,
      .font_index         = TITLE_FONT,
      .font_size          = TITLE_FONT_SIZE,
      .indent             =     0,
      .margin_left        =     0,
      .margin_right       =     0,
      .margin_top         =     0,
      .margin_bottom      =     0,
      .screen_left        =  xpos,
      .screen_right       =    10,
      .screen_top         =  ypos,
      .screen_bottom      = (int16_t)(Screen::get_height() - (ypos + BooksDir::max_cover_width + 20)),
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

  Font * title_font  = fonts.get(TITLE_FONT);
  Font * author_font = fonts.get(AUTHOR_FONT);
  const int16_t max_text_w = (int16_t)(Screen::get_width() - 10 - xpos);

  for (int item_idx = 0; book_idx < last; item_idx++, book_idx++) {

    int16_t top_pos = ypos;

    const BooksDir::EBookRecord * book = books_dir.get_book_data(book_idx);

    if (book == nullptr) break;

    #if defined(BOARD_TYPE_PAPER_S3)
      const int16_t cover_x = 10;
      const int16_t cover_y = ypos;
      const int16_t cover_w = cover_box_w;
      const int16_t cover_h = cover_box_h;

      page.clear_region(Dim(cover_w, cover_h), Pos(cover_x, cover_y));
      page.put_highlight(Dim(cover_w, cover_h), Pos(cover_x, cover_y));

      static constexpr int16_t PLACEHOLDER_SIZE = 8;
      Page::Format ph_fmt = fmt;
      ph_fmt.font_index = 1;
      ph_fmt.font_size  = PLACEHOLDER_SIZE;
      ph_fmt.font_style = Fonts::FaceStyle::NORMAL;
      ph_fmt.align      = CSS::Align::CENTER;

      Font * ph_font = fonts.get(ph_fmt.font_index);
      if (ph_font != nullptr) {
        const int16_t ascent = (int16_t)ph_font->get_chars_height(PLACEHOLDER_SIZE);
        const int16_t line_h = (int16_t)ph_font->get_line_height(PLACEHOLDER_SIZE);
        const int16_t total_h = (int16_t)(3 * line_h);

        int16_t top = (int16_t)(cover_y + ((cover_h - total_h) >> 1));
        if (top < cover_y) top = cover_y;

        const int16_t cx = (int16_t)(cover_x + (cover_w >> 1));
        page.put_str_at("Cover", Pos(cx, (int16_t)(top + ascent)), ph_fmt);
        page.put_str_at("not", Pos(cx, (int16_t)(top + line_h + ascent)), ph_fmt);
        page.put_str_at("available", Pos(cx, (int16_t)(top + (2 * line_h) + ascent)), ph_fmt);
      }
    #else
      Image::ImageData image(Dim(book->cover_width, book->cover_height), 
                             (uint8_t *) book->cover_bitmap);
      page.put_image(image, Pos(10 + books_dir.MAX_COVER_WIDTH - book->cover_width, ypos));
    #endif

    #if !(INKPLATE_6PLUS || TOUCH_TRIAL)
      if (item_idx == current_item_idx) {
        page.put_highlight(Dim(Screen::get_width() - (25 + BooksDir::max_cover_width), 
                               BooksDir::max_cover_height), 
                           Pos(xpos - 5, ypos));
      }
    #endif

    #if defined(BOARD_TYPE_PAPER_S3)
      std::string title_text;
      #if EPUB_INKPLATE_BUILD
        if (nvs_mgr.id_exists(book->id)) {
          title_text = std::string("[Reading] ") + book->title;
        }
        else {
          title_text = book->title;
        }
      #else
        title_text = book->title;
      #endif

      const std::vector<std::string> title_lines = wrap_to_width(*title_font, title_text.c_str(), max_text_w, (int8_t)TITLE_FONT_SIZE);
      const std::vector<std::string> author_lines = wrap_to_width(*author_font, book->author, max_text_w, (int8_t)AUTHOR_FONT_SIZE);

      const int16_t title_ascent = (int16_t)title_font->get_chars_height(TITLE_FONT_SIZE);
      const int16_t author_ascent = (int16_t)author_font->get_chars_height(AUTHOR_FONT_SIZE);
      const int16_t title_line_h = (int16_t)(title_font->get_line_height(TITLE_FONT_SIZE) * 0.8f);
      const int16_t author_line_h = (int16_t)(author_font->get_line_height(AUTHOR_FONT_SIZE) * 0.8f);
      const int16_t gap = 2;
      const int16_t line_gap = 4;

      std::vector<std::string> t = title_lines;
      std::vector<std::string> a = author_lines;

      if (t.empty()) t.emplace_back();
      if (a.empty()) a.emplace_back();

      auto calc_h = [&]() {
        const int16_t th = (int16_t)((t.size() * title_line_h) + ((t.size() > 0) ? ((t.size() - 1) * line_gap) : 0));
        const int16_t ah = (int16_t)((a.size() * author_line_h) + ((a.size() > 0) ? ((a.size() - 1) * line_gap) : 0));
        return (int16_t)(th + gap + ah);
      };

      int16_t total_h = calc_h();
      bool truncated = false;

      while ((total_h > cover_box_h) && ((a.size() > 1) || (t.size() > 1))) {
        truncated = true;
        if (a.size() > 1) a.pop_back();
        else if (t.size() > 1) t.pop_back();
        total_h = calc_h();
      }

      if (truncated) {
        if (!a.empty()) {
          a.back() = truncate_to_width(*author_font, a.back().c_str(), max_text_w, (int8_t)AUTHOR_FONT_SIZE);
        }
        else if (!t.empty()) {
          t.back() = truncate_to_width(*title_font, t.back().c_str(), max_text_w, (int8_t)TITLE_FONT_SIZE);
        }
      }

      int16_t top = (int16_t)(ypos + ((cover_box_h - total_h) >> 1));
      if (top < ypos) top = ypos;

      Page::Format title_fmt = fmt;
      title_fmt.font_index = TITLE_FONT;
      title_fmt.font_size  = TITLE_FONT_SIZE;
      title_fmt.font_style = Fonts::FaceStyle::NORMAL;
      title_fmt.align      = CSS::Align::LEFT;

      Page::Format author_fmt = fmt;
      author_fmt.font_index = AUTHOR_FONT;
      author_fmt.font_size  = AUTHOR_FONT_SIZE;
      author_fmt.font_style = Fonts::FaceStyle::ITALIC;
      author_fmt.align      = CSS::Align::LEFT;

      int16_t y = top;
      for (const auto & line : t) {
        page.put_str_at(line, Pos(xpos, (int16_t)(y + title_ascent)), title_fmt);
        y = (int16_t)(y + title_line_h + line_gap);
      }
      y = (int16_t)(y + gap - line_gap);
      for (const auto & line : a) {
        page.put_str_at(line, Pos(xpos, (int16_t)(y + author_ascent)), author_fmt);
        y = (int16_t)(y + author_line_h + line_gap);
      }
    #else
      fmt.font_index    = TITLE_FONT;
      fmt.font_size     = TITLE_FONT_SIZE;
      fmt.font_style    = Fonts::FaceStyle::NORMAL;
      fmt.screen_top    = ypos;
      fmt.screen_bottom = (int16_t)(Screen::get_height() - (ypos + row_stride));

      page.set_limits(fmt);
      page.new_paragraph(fmt);
      #if EPUB_INKPLATE_BUILD
        if (nvs_mgr.id_exists(book->id)) {
          std::string line = std::string("[Reading] ") + book->title;
          page.add_text(truncate_to_width(*title_font, line.c_str(), max_text_w, (int8_t)TITLE_FONT_SIZE), fmt);
        }
        else {
          page.add_text(truncate_to_width(*title_font, book->title, max_text_w, (int8_t)TITLE_FONT_SIZE), fmt);
        }
      #else
        page.add_text(truncate_to_width(*title_font, book->title, max_text_w, (int8_t)TITLE_FONT_SIZE), fmt);
      #endif
      page.end_paragraph(fmt);

      fmt.font_index = AUTHOR_FONT;
      fmt.font_size  = AUTHOR_FONT_SIZE;
      fmt.font_style = Fonts::FaceStyle::ITALIC;

      page.new_paragraph(fmt);
      page.add_text(truncate_to_width(*author_font, book->author, max_text_w, (int8_t)AUTHOR_FONT_SIZE), fmt);
      page.end_paragraph(fmt);
    #endif

    ypos = top_pos + row_stride;
  }

  ScreenBottom::show(page_nbr, page_count);
  
  page.paint();
}

void 
LinearBooksDirViewer::highlight(int16_t item_idx)
{
  #if !(INKPLATE_6PLUS || TOUCH_TRIAL)
  page.set_compute_mode(Page::ComputeMode::DISPLAY);

  if (current_item_idx != item_idx) {

    // Clear the highlighting of the current item

    int16_t book_idx = current_page_nbr * books_per_page + current_item_idx;

    int16_t xpos = 20 + BooksDir::max_cover_width;
    int16_t ypos = FIRST_ENTRY_YPOS + (current_item_idx * (BooksDir::max_cover_height + SPACE_BETWEEN_ENTRIES));

    const BooksDir::EBookRecord * book = books_dir.get_book_data(book_idx);

    if (book == nullptr) return;

    // TTF * font = fonts.get(1, 9);

    Page::Format fmt = {
      .line_height_factor = 0.8,
      .font_index         = TITLE_FONT,
      .font_size          = TITLE_FONT_SIZE,
      .indent             = 0,
      .margin_left        = 0,
      .margin_right       = 0,
      .margin_top         = 0,
      .margin_bottom      = 0,
      .screen_left        = xpos,
      .screen_right       = 10,
      .screen_top         = ypos,
      .screen_bottom      = (int16_t)(Screen::get_height() - (ypos + BooksDir::max_cover_width + 20)),
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

    page.start(fmt);

    page.clear_highlight(
      Dim(Screen::get_width() - (25 + BooksDir::max_cover_width), BooksDir::max_cover_height),
      Pos(xpos - 5, ypos));

    page.set_limits(fmt);
    page.new_paragraph(fmt);
    #if EPUB_INKPLATE_BUILD
      if (nvs_mgr.id_exists(book->id)) page.add_text("[Reading] ", fmt);
    #endif
    page.add_text(book->title, fmt);
    page.end_paragraph(fmt);

    fmt.font_index = AUTHOR_FONT;
    fmt.font_size  = AUTHOR_FONT_SIZE;
    fmt.font_style = Fonts::FaceStyle::ITALIC;

    page.new_paragraph(fmt);
    page.add_text(book->author, fmt);
    page.end_paragraph(fmt);
    
    // Highlight the new current item

    current_item_idx = item_idx;

    book_idx = current_page_nbr * books_per_page + current_item_idx;
    ypos = FIRST_ENTRY_YPOS + (current_item_idx * (BooksDir::max_cover_height + 6));

    book = books_dir.get_book_data(book_idx);

    if (book == nullptr) return;
    
    page.put_highlight(
      Dim(Screen::get_width() - (25 + BooksDir::max_cover_width), BooksDir::max_cover_height),
      Pos(xpos - 5, ypos));


    fmt.font_index    = TITLE_FONT;
    fmt.font_size     = TITLE_FONT_SIZE;
    fmt.font_style    = Fonts::FaceStyle::NORMAL;
    fmt.screen_top    = ypos;
    fmt.screen_bottom = (int16_t)(Screen::get_height() - (ypos + BooksDir::max_cover_width + 20));

    page.set_limits(fmt);
    page.new_paragraph(fmt);
    #if EPUB_INKPLATE_BUILD
      if (nvs_mgr.id_exists(book->id)) page.add_text("[Reading] ", fmt);
    #endif
    page.add_text(book->title, fmt);
    page.end_paragraph(fmt);

    fmt.font_index = AUTHOR_FONT,
    fmt.font_size  = AUTHOR_FONT_SIZE,
    fmt.font_style = Fonts::FaceStyle::ITALIC,

    page.new_paragraph(fmt);
    page.add_text(book->author, fmt);
    page.end_paragraph(fmt);

    #if EPUB_INKPLATE_BUILD && !BOARD_TYPE_PAPER_S3
      BatteryViewer::show();
    #endif

    page.paint(false);
  }
  #endif
}

int16_t
LinearBooksDirViewer::show_page_and_highlight(int16_t book_idx)
{
  int16_t page_nbr = book_idx / books_per_page;
  int16_t item_idx = book_idx % books_per_page;

  if (current_page_nbr != page_nbr) {
    show_page(page_nbr, item_idx);
  }
  else {
    if (item_idx != current_item_idx) highlight(item_idx);
  }
  current_book_idx = book_idx;
  return current_book_idx;
}

void
LinearBooksDirViewer::highlight_book(int16_t book_idx)
{
  highlight(book_idx % books_per_page);  
  current_book_idx = book_idx;
}

int16_t
LinearBooksDirViewer::next_page()
{
  return next_column();
}

int16_t
LinearBooksDirViewer::prev_page()
{
  return prev_column();
}

int16_t
LinearBooksDirViewer::next_item()
{
  int16_t book_idx = current_book_idx + 1;
  if (book_idx >= books_dir.get_book_count()) {
    book_idx = books_dir.get_book_count() - 1;
  }
  return show_page_and_highlight(book_idx);
}

int16_t
LinearBooksDirViewer::prev_item()
{
  int16_t book_idx = current_book_idx - 1;
  if (book_idx < 0) book_idx = 0;
  return show_page_and_highlight(book_idx);
}

int16_t
LinearBooksDirViewer::next_column()
{
  int16_t book_idx = current_book_idx + books_per_page;
  if (book_idx >= books_dir.get_book_count()) {
    book_idx = books_dir.get_book_count() - 1;
  }
  else {
    book_idx = (book_idx / books_per_page) * books_per_page;
  }
  return show_page_and_highlight(book_idx);
}

int16_t
LinearBooksDirViewer::prev_column()
{
  int16_t book_idx = current_book_idx - books_per_page;
  if (book_idx < 0) book_idx = 0;
  else book_idx = (book_idx / books_per_page) * books_per_page;
  return show_page_and_highlight(book_idx);
}
