// Copyright (c) 2021 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.

#define __MATRIX_BOOKS_DIR_VIEWER__ 1
#include "viewers/matrix_books_dir_viewer.hpp"

#include "models/fonts.hpp"
#include "models/config.hpp"
#include "viewers/page.hpp"
#include "viewers/screen_bottom.hpp"

#include "models/default_cover.hpp"

#if EPUB_INKPLATE_BUILD
  #include "viewers/battery_viewer.hpp"
  #include "models/nvs_mgr.hpp"
#endif

#include "screen.hpp"

#include "stb_image_resize.h"

#include <iomanip>

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

#if (INKPLATE_6PLUS || TOUCH_TRIAL)
  static const std::string TOUCH_AND_HOLD_STR = "Touch and hold cover for info. Tap to open.";
#endif

void
MatrixBooksDirViewer::setup()
{
  Font * font = fonts.get(TITLE_FONT);
  title_font_height = font->get_line_height(TITLE_FONT_SIZE) * 0.8;

  font = fonts.get(AUTHOR_FONT);
  author_font_height = font->get_line_height(AUTHOR_FONT_SIZE) * 0.8;

  font = fonts.get(ScreenBottom::FONT);
  pagenbr_font_height = font->get_line_height(ScreenBottom::FONT_SIZE);

  first_entry_ypos = (title_font_height << 1) + author_font_height + SPACE_BELOW_INFO + 10;

  #if defined(BOARD_TYPE_PAPER_S3)
    grid_left   = 18;
    column_count = 2;
    line_count   = 2;

    static constexpr int16_t GAP_X = 18;
    static constexpr int16_t GAP_Y = 18;
    const int16_t grid_right = (int16_t)(Screen::get_width() - grid_left);

    cover_box_w = (int16_t)((grid_right - grid_left - GAP_X) / 2);
    if (cover_box_w < 1) cover_box_w = 1;

    static constexpr int16_t TEXT_GAP_AFTER_COVER   = 4;
    static constexpr int16_t TEXT_GAP_BETWEEN_LINES = 2;

    first_entry_ypos = 10;

    Font * title_font  = fonts.get(TITLE_FONT);
    Font * author_font = fonts.get(AUTHOR_FONT);
    const int16_t title_line_h  = (int16_t)(title_font->get_line_height(TITLE_FONT_SIZE) * 0.8f);
    const int16_t author_line_h = (int16_t)(author_font->get_line_height(AUTHOR_FONT_SIZE) * 0.8f);
    const int16_t text_block_h  = (int16_t)(TEXT_GAP_AFTER_COVER + title_line_h + TEXT_GAP_BETWEEN_LINES + author_line_h);

    const int16_t bottom_reserved = (int16_t)(pagenbr_font_height + SPACE_ABOVE_PAGENBR + 10);
    const int16_t avail_h = (int16_t)(Screen::get_height() - first_entry_ypos - bottom_reserved);

    const int16_t cover_avail_h = (int16_t)(avail_h - GAP_Y - (2 * text_block_h));
    cover_box_h = (int16_t)(cover_avail_h / 2);
    if (cover_box_h < 1) cover_box_h = 1;

    item_box_h = (int16_t)(cover_box_h + text_block_h);

    horiz_space_between_entries = (uint8_t)GAP_X;
    vert_space_between_entries  = (uint8_t)GAP_Y;

    books_per_page = 4;
    page_count = (books_dir.get_book_count() + books_per_page - 1) / books_per_page;
  #else

  line_count = (Screen::get_height() - first_entry_ypos - pagenbr_font_height - SPACE_ABOVE_PAGENBR + MIN_SPACE_BETWEEN_ENTRIES) / 
                (BooksDir::max_cover_height + MIN_SPACE_BETWEEN_ENTRIES);

  column_count = (Screen::get_width() - 10 + MIN_SPACE_BETWEEN_ENTRIES) / (BooksDir::max_cover_width + MIN_SPACE_BETWEEN_ENTRIES);

  horiz_space_between_entries = (Screen::get_width() - 10 - (BooksDir::max_cover_width * column_count)) / (column_count - 1);
  vert_space_between_entries  = (Screen::get_height() - first_entry_ypos - pagenbr_font_height - SPACE_ABOVE_PAGENBR - (BooksDir::max_cover_height * line_count)) / (line_count - 1);
  books_per_page              = line_count * column_count;
  page_count                  = (books_dir.get_book_count() + books_per_page - 1) / books_per_page;
  item_box_h                  = BooksDir::max_cover_height;
  #endif

  log('I', TAG,
      "MatrixBooksDir setup: screen=%dx%d cover=%dx%d columns=%d lines=%d hspace=%d vspace=%d books_per_page=%d page_count=%d",
      Screen::get_width(), Screen::get_height(),
      BooksDir::max_cover_width, BooksDir::max_cover_height,
      column_count, line_count,
      horiz_space_between_entries, vert_space_between_entries,
      books_per_page, page_count);

  current_page_nbr = -1;
  current_book_idx = -1;
  current_item_idx = -1;
}

void 
MatrixBooksDirViewer::show_page(int16_t page_nbr, int16_t hightlight_item_idx)
{
  current_page_nbr = page_nbr;    // First page == 0
  current_item_idx = hightlight_item_idx;

  int16_t book_idx = page_nbr * books_per_page;
  int16_t last     = book_idx + books_per_page;

  page.set_compute_mode(Page::ComputeMode::DISPLAY);

  if (last > books_dir.get_book_count()) last = books_dir.get_book_count();

  int16_t xpos = 5;
  int16_t ypos = first_entry_ypos;
  #if !defined(BOARD_TYPE_PAPER_S3)
    int16_t line_pos = 0;
  #endif

  Page::Format fmt = {
      .line_height_factor =   0.8,
      .font_index         = TITLE_FONT,
      .font_size          = TITLE_FONT_SIZE,
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

  Font * title_font  = fonts.get(TITLE_FONT);
  Font * author_font = fonts.get(AUTHOR_FONT);

  for (int item_idx = 0; book_idx < last; item_idx++, book_idx++) {

    const BooksDir::EBookRecord * book = books_dir.get_book_data(book_idx);

    if (book == nullptr) break;

    // Compute base position for this item.
    int16_t draw_x = xpos;
    int16_t draw_y = ypos;

    #if defined(BOARD_TYPE_PAPER_S3)
      int16_t row = item_idx / column_count;
      int16_t col = item_idx % column_count;
      draw_x = grid_left + ((cover_box_w + horiz_space_between_entries) * col);
      draw_y = first_entry_ypos + ((item_box_h + vert_space_between_entries) * row);
    #endif

    #if defined(BOARD_TYPE_PAPER_S3)
      page.clear_region(Dim(cover_box_w, cover_box_h), Pos(draw_x, draw_y));
      page.put_highlight(Dim(cover_box_w, cover_box_h), Pos(draw_x, draw_y));

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

        int16_t top = (int16_t)(draw_y + ((cover_box_h - total_h) >> 1));
        if (top < draw_y) top = draw_y;

        const int16_t cx = (int16_t)(draw_x + (cover_box_w >> 1));
        page.put_str_at("Cover", Pos(cx, (int16_t)(top + ascent)), ph_fmt);
        page.put_str_at("not", Pos(cx, (int16_t)(top + line_h + ascent)), ph_fmt);
        page.put_str_at("available", Pos(cx, (int16_t)(top + (2 * line_h) + ascent)), ph_fmt);
      }
    #else
      Image::ImageData image(Dim(book->cover_width, book->cover_height), (uint8_t *) book->cover_bitmap);
      page.put_image(image, Pos(draw_x + ((BooksDir::MAX_COVER_WIDTH - book->cover_width) >> 1), 
                                draw_y + ((BooksDir::MAX_COVER_HEIGHT - book->cover_height) >> 1)));
    #endif

    #if defined(BOARD_TYPE_PAPER_S3)
      static constexpr int16_t TEXT_GAP_AFTER_COVER   = 4;
      static constexpr int16_t TEXT_GAP_BETWEEN_LINES = 2;
      const int16_t pad_x = 2;
      const int16_t max_text_w = (int16_t)(cover_box_w - (pad_x << 1));

      const int16_t title_ascent = (int16_t)title_font->get_chars_height(TITLE_FONT_SIZE);
      const int16_t author_ascent = (int16_t)author_font->get_chars_height(AUTHOR_FONT_SIZE);
      const int16_t title_line_h = (int16_t)(title_font->get_line_height(TITLE_FONT_SIZE) * 0.8f);

      const int16_t title_top = (int16_t)(draw_y + cover_box_h + TEXT_GAP_AFTER_COVER);
      const int16_t title_baseline = (int16_t)(title_top + title_ascent);
      const int16_t author_top = (int16_t)(title_top + title_line_h + TEXT_GAP_BETWEEN_LINES);
      const int16_t author_baseline = (int16_t)(author_top + author_ascent);

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

      const std::string title = truncate_to_width(*title_font, book->title, max_text_w, (int8_t)TITLE_FONT_SIZE);
      const std::string author = truncate_to_width(*author_font, book->author, max_text_w, (int8_t)AUTHOR_FONT_SIZE);

      page.put_str_at(title, Pos((int16_t)(draw_x + pad_x), title_baseline), title_fmt);
      page.put_str_at(author, Pos((int16_t)(draw_x + pad_x), author_baseline), author_fmt);
    #endif

    #if !(INKPLATE_6PLUS || TOUCH_TRIAL)
      if (item_idx == current_item_idx) {
        page.put_highlight(Dim(BooksDir::max_cover_width + 4, BooksDir::max_cover_height + 4), 
                           Pos(draw_x - 2, draw_y - 2));
        page.put_highlight(Dim(BooksDir::max_cover_width + 6, BooksDir::max_cover_height + 6), 
                           Pos(draw_x - 3, draw_y - 3));

        fmt.font_index    = TITLE_FONT;
        fmt.font_size     = TITLE_FONT_SIZE;
        fmt.font_style    = Fonts::FaceStyle::NORMAL;

        char title[MAX_TITLE_SIZE];
        title[MAX_TITLE_SIZE - 1] = 0;
        strncpy(title, book->title, MAX_TITLE_SIZE - 1);
        if (strlen(book->title) > (MAX_TITLE_SIZE - 1)) {
          strcpy(&title[MAX_TITLE_SIZE - 5], " ...");
        }

        page.set_limits(fmt);
        page.new_paragraph(fmt);
        #if EPUB_INKPLATE_BUILD
          if (nvs_mgr.id_exists(book->id)) page.add_text("[Reading] ", fmt);
        #endif
        page.add_text(title, fmt);
        page.end_paragraph(fmt);

        fmt.font_index = AUTHOR_FONT;
        fmt.font_size  = AUTHOR_FONT_SIZE;
        fmt.font_style = Fonts::FaceStyle::ITALIC;

        page.new_paragraph(fmt);
        page.add_text(book->author, fmt);
        page.end_paragraph(fmt);
      }
    #endif

    #if !defined(BOARD_TYPE_PAPER_S3)
      line_pos++;

      if (line_pos >= line_count) {
        xpos    += BooksDir::MAX_COVER_WIDTH + horiz_space_between_entries;
        ypos     = first_entry_ypos;
        line_pos = 0;
      }
      else {
        ypos += BooksDir::MAX_COVER_HEIGHT + vert_space_between_entries;
      }
    #endif
  }

  #if (INKPLATE_6PLUS || TOUCH_TRIAL)
    #if defined(BOARD_TYPE_PAPER_S3)
      { }
    #else
      fmt.screen_top = 10 + title_font_height;
      page.set_limits(fmt);
      page.new_paragraph(fmt);
      page.add_text(TOUCH_AND_HOLD_STR, fmt);
      page.end_paragraph(fmt);
    #endif
  #endif

  #if !defined(BOARD_TYPE_PAPER_S3)
    page.put_highlight(Dim(Screen::get_width() - 20, 3), Pos(10, first_entry_ypos - 8));
  #endif

  ScreenBottom::show(current_page_nbr, page_count);

  page.paint();
}

void 
MatrixBooksDirViewer::highlight(int16_t item_idx)
{
  int16_t book_idx,
          column_idx, line_idx,
          xpos, ypos;

  const BooksDir::EBookRecord * book;

  Page::Format fmt = {
    .line_height_factor = 0.8,
    .font_index         = TITLE_FONT,
    .font_size          = TITLE_FONT_SIZE,
    .indent             = 0,
    .margin_left        = 0,
    .margin_right       = 0,
    .margin_top         = 0,
    .margin_bottom      = 0,
    .screen_left        = 10,
    .screen_right       = 10,
    .screen_top         = 10,
    .screen_bottom      = 100,
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

  page.set_compute_mode(Page::ComputeMode::DISPLAY);

  page.start(fmt);

  if ((current_item_idx != -1) && (current_item_idx != item_idx)) {

    // Clear the highlighting of the current item

    book_idx = current_page_nbr * books_per_page + current_item_idx;

    #if defined(BOARD_TYPE_PAPER_S3)
      column_idx = current_item_idx % column_count;
      line_idx   = current_item_idx / column_count;
    #else
      column_idx = current_item_idx / line_count;
      line_idx   = current_item_idx % line_count;
    #endif

    #if defined(BOARD_TYPE_PAPER_S3)
      xpos = grid_left + ((cover_box_w + horiz_space_between_entries) * column_idx);
      ypos = first_entry_ypos + ((item_box_h + vert_space_between_entries) * line_idx);
    #else
      xpos = 5 + ((BooksDir::max_cover_width + horiz_space_between_entries) * column_idx);
      ypos = first_entry_ypos + ((BooksDir::max_cover_height + vert_space_between_entries) * line_idx);
    #endif

    book = books_dir.get_book_data(book_idx);

    if (book == nullptr) return;

    // Font * font = fonts.get(1, 9);

    #if defined(BOARD_TYPE_PAPER_S3)
      page.clear_highlight(Dim(cover_box_w + 4, cover_box_h + 4), Pos(xpos - 2, ypos - 2));
      page.clear_highlight(Dim(cover_box_w + 6, cover_box_h + 6), Pos(xpos - 3, ypos - 3));
    #else
      page.clear_highlight(Dim(BooksDir::max_cover_width + 4, BooksDir::max_cover_height + 4), 
                           Pos(xpos - 2, ypos - 2));
      page.clear_highlight(Dim(BooksDir::max_cover_width + 6, BooksDir::max_cover_height + 6), 
                           Pos(xpos - 3, ypos - 3));
    #endif

    page.clear_region(Dim(Screen::get_width() - 10, (title_font_height << 1) + author_font_height),
                      Pos(10, 10));
  }
    // Highlight the new current item

  current_item_idx = -1;

  book_idx = current_page_nbr * books_per_page + item_idx;

  book = books_dir.get_book_data(book_idx);

  if (book == nullptr) return;
  
  current_item_idx = item_idx;

  #if defined(BOARD_TYPE_PAPER_S3)
    column_idx = current_item_idx % column_count;
    line_idx   = current_item_idx / column_count;
  #else
    column_idx = current_item_idx / line_count;
    line_idx   = current_item_idx % line_count;
  #endif

  #if defined(BOARD_TYPE_PAPER_S3)
    xpos = grid_left + ((cover_box_w + horiz_space_between_entries) * column_idx);
    ypos = first_entry_ypos + ((item_box_h + vert_space_between_entries) * line_idx);
  #else
    xpos = 5 + ((BooksDir::max_cover_width + horiz_space_between_entries) * column_idx);
    ypos = first_entry_ypos + ((BooksDir::max_cover_height + vert_space_between_entries) * line_idx);
  #endif

  #if defined(BOARD_TYPE_PAPER_S3)
    page.put_highlight(Dim(cover_box_w + 4, cover_box_h + 4), Pos(xpos - 2, ypos - 2));
    page.put_highlight(Dim(cover_box_w + 6, cover_box_h + 6), Pos(xpos - 3, ypos - 3));
  #else
    page.put_highlight(Dim(BooksDir::max_cover_width + 4, BooksDir::max_cover_height + 4), 
                        Pos(xpos - 2, ypos - 2));
    page.put_highlight(Dim(BooksDir::max_cover_width + 6, BooksDir::max_cover_height + 6), 
                        Pos(xpos - 3, ypos - 3));
  #endif

  page.clear_region(Dim(Screen::get_width() - 10, (title_font_height << 1) + author_font_height),
                    Pos(10, 10));

  fmt.font_index    = TITLE_FONT;
  fmt.font_size     = TITLE_FONT_SIZE;
  fmt.font_style    = Fonts::FaceStyle::NORMAL;

  char title[MAX_TITLE_SIZE];
  title[MAX_TITLE_SIZE - 1] = 0;
  strncpy(title, book->title, MAX_TITLE_SIZE - 1);
  if (strlen(book->title) > (MAX_TITLE_SIZE - 1)) {
    strcpy(&title[MAX_TITLE_SIZE - 5], " ...");
  }

  page.set_limits(fmt);
  page.new_paragraph(fmt);
  #if EPUB_INKPLATE_BUILD
    if (nvs_mgr.id_exists(book->id)) page.add_text("[Reading] ", fmt);
  #endif
  page.add_text(title, fmt);
  page.end_paragraph(fmt);

  fmt.font_index = AUTHOR_FONT;
  fmt.font_size  = AUTHOR_FONT_SIZE;
  fmt.font_style = Fonts::FaceStyle::ITALIC;

  page.new_paragraph(fmt);
  page.add_text(book->author, fmt);
  page.end_paragraph(fmt);

  ScreenBottom::show(current_page_nbr, page_count);

  page.paint(false);
}

void 
MatrixBooksDirViewer::clear_highlight()
{
  #if (INKPLATE_6PLUS || TOUCH_TRIAL)
    return;
  #endif

  if (current_item_idx == -1) return;

  page.set_compute_mode(Page::ComputeMode::DISPLAY);

  // Clear the highlighting of the current item

  int16_t book_idx = current_page_nbr * books_per_page + current_item_idx;

  #if defined(BOARD_TYPE_PAPER_S3)
    int16_t column_idx = current_item_idx % column_count;
    int16_t line_idx   = current_item_idx / column_count;
  #else
    int16_t column_idx = current_item_idx / line_count;
    int16_t line_idx   = current_item_idx % line_count;
  #endif

  int16_t xpos;
  int16_t ypos;
  #if defined(BOARD_TYPE_PAPER_S3)
    xpos = grid_left + ((cover_box_w + horiz_space_between_entries) * column_idx);
    ypos = first_entry_ypos + ((item_box_h + vert_space_between_entries) * line_idx);
  #else
    xpos = 5 + ((BooksDir::max_cover_width + horiz_space_between_entries) * column_idx);
    ypos = first_entry_ypos + ((BooksDir::max_cover_height + vert_space_between_entries) * line_idx);
  #endif

  const BooksDir::EBookRecord * book = books_dir.get_book_data(book_idx);

  if (book == nullptr) return;

  // Font * font = fonts.get(1, 9);

  Page::Format fmt = {
    .line_height_factor = 0.8,
    .font_index         = TITLE_FONT,
    .font_size          = TITLE_FONT_SIZE,
    .indent             = 0,
    .margin_left        = 0,
    .margin_right       = 0,
    .margin_top         = 0,
    .margin_bottom      = 0,
    .screen_left        = 10,
    .screen_right       = 10,
    .screen_top         = 10,
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

  #if defined(BOARD_TYPE_PAPER_S3)
    page.clear_highlight(Dim(cover_box_w + 4, cover_box_h + 4), Pos(xpos - 2, ypos - 2));
    page.clear_highlight(Dim(cover_box_w + 6, cover_box_h + 6), Pos(xpos - 3, ypos - 3));
  #else
    page.clear_highlight(Dim(BooksDir::max_cover_width + 4, BooksDir::max_cover_height + 4), 
                          Pos(xpos - 2, ypos - 2));
    page.clear_highlight(Dim(BooksDir::max_cover_width + 6, BooksDir::max_cover_height + 6), 
                          Pos(xpos - 3, ypos - 3));
  #endif

  page.clear_region(Dim(Screen::get_width() - 10, (title_font_height << 1) + author_font_height),
                    Pos(10, 10));

  #if (INKPLATE_6PLUS || TOUCH_TRIAL)
    #if defined(BOARD_TYPE_PAPER_S3)
      {
        static constexpr int16_t HINT_SIZE = 8;
        Font * hint_font = fonts.get(1);
        if (hint_font != nullptr) {
          Page::Format hint_fmt = fmt;
          hint_fmt.font_index = 1;
          hint_fmt.font_size  = HINT_SIZE;
          hint_fmt.align      = CSS::Align::LEFT;

          const int16_t pad_x  = 10;
          const int16_t pad_y  = 2;
          const int16_t ascent = (int16_t)hint_font->get_chars_height(HINT_SIZE);
          const int16_t y      = 10 + pad_y + ascent;
          page.put_str_at(TOUCH_AND_HOLD_STR, Pos(pad_x, y), hint_fmt);
        }
      }
    #else
      fmt.screen_top = 10 + title_font_height;
      page.set_limits(fmt);
      page.new_paragraph(fmt);
      page.add_text(TOUCH_AND_HOLD_STR, fmt);
      page.end_paragraph(fmt);
    #endif
  #endif

  #if EPUB_INKPLATE_BUILD && !BOARD_TYPE_PAPER_S3
    BatteryViewer::show();
  #endif

  page.paint(false);

  current_item_idx = -1;
}

int16_t
MatrixBooksDirViewer::show_page_and_highlight(int16_t book_idx)
{
  int16_t page_nbr = book_idx / books_per_page;
  int16_t item_idx = book_idx % books_per_page;

  if (current_page_nbr != page_nbr) {
    show_page(page_nbr, item_idx);
    current_page_nbr = page_nbr;
  }
  else {
    if (item_idx != current_item_idx) highlight(item_idx);
  }
  current_book_idx = book_idx;
  return current_book_idx;
}

void
MatrixBooksDirViewer::highlight_book(int16_t book_idx)
{
  #if (INKPLATE_6PLUS || TOUCH_TRIAL)
    (void)book_idx;
    return;
  #else
    highlight(book_idx % books_per_page);
  #endif
}

int16_t
MatrixBooksDirViewer::next_page()
{
  int16_t page_nbr = current_page_nbr + 1;
  if (page_nbr >= page_count) {
    page_nbr = page_count - 1;
  }
  if (current_page_nbr != page_nbr) {
    show_page(page_nbr, 0);
    current_book_idx = page_nbr * books_per_page;
  }
  else if ((page_nbr + 1) == page_count) {
    #if !(INKPLATE_6PLUS || TOUCH_TRIAL)
      highlight(books_dir.get_book_count() % books_per_page - 1);
    #endif
    current_book_idx = books_dir.get_book_count() - 1;
  }
  return current_book_idx;
}

int16_t
MatrixBooksDirViewer::prev_page()
{
  int16_t page_nbr = current_page_nbr - 1;
  if (page_nbr < 0) {
    page_nbr = 0;
  }
  if (current_page_nbr != page_nbr) {
    show_page(page_nbr, 0);
  }
  else {
    #if !(INKPLATE_6PLUS || TOUCH_TRIAL)
      highlight(0);
    #endif
  }
  current_book_idx = page_nbr * books_per_page;
  return current_book_idx;
}

int16_t
MatrixBooksDirViewer::next_item()
{
  int16_t book_idx = current_book_idx + 1;
  if (book_idx >= books_dir.get_book_count()) {
    book_idx = books_dir.get_book_count() - 1;
  }
  return show_page_and_highlight(book_idx);
}

int16_t
MatrixBooksDirViewer::prev_item()
{
  int16_t book_idx = current_book_idx - 1;
  if (book_idx < 0) book_idx = 0;
  return show_page_and_highlight(book_idx);
}

int16_t
MatrixBooksDirViewer::next_column()
{
  int16_t book_idx = current_book_idx + line_count;
  if (book_idx >= books_dir.get_book_count()) {
    book_idx = books_dir.get_book_count() - 1;
  }
  return show_page_and_highlight(book_idx);
}

int16_t
MatrixBooksDirViewer::prev_column()
{
  int16_t book_idx = current_book_idx - line_count;
  if (book_idx < 0) book_idx = 0;
  return show_page_and_highlight(book_idx);
}
