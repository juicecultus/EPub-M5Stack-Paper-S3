// Copyright (c) 2020 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.

#pragma once
#include "global.hpp"

#include "controllers/event_mgr.hpp"
#include "models/fonts.hpp"
#include "viewers/page.hpp"
#include "viewers/keypad_viewer.hpp"
#include "viewers/screen_bottom.hpp"
#include "memory_pool.hpp"

#include <list>

#include <vector>

 #include <cstdio>
 #include <cstring>

enum class FormEntryType { HORIZONTAL, VERTICAL, UINT16
  #if INKPLATE_6PLUS || TOUCH_TRIAL
    , DONE
  #endif
};

#if defined(BOARD_TYPE_PAPER_S3)
constexpr  uint8_t FORM_FONT_SIZE = 18;
constexpr  uint8_t FORM_LABEL_FONT_SIZE = 8;
constexpr  uint8_t FORM_VALUE_FONT_SIZE = 12;
#else
constexpr  uint8_t FORM_FONT_SIZE = 9;
#endif

struct FormChoice {
  const char   * caption;
  int8_t         value;
};

struct FormEntry {
  const char       * caption;
  union {
    struct {
      int8_t           * value;
      int8_t             choice_count;
      const FormChoice * choices;
    } ch;
    struct {
      uint16_t * value;
      uint16_t   min;
      uint16_t   max;
    } val;
  } u;
  FormEntryType      entry_type;
};

class FormField
{
  public:
    FormField(FormEntry & form_entry, Font & font) :
      form_entry(form_entry), font(font), event_control(false) { 
    };
    virtual ~FormField() { };

    virtual const Dim    get_field_dim() { return field_dim;   }
    inline const Dim & get_caption_dim() { return caption_dim; }

    inline const Pos &   get_field_pos() { return field_pos;   }
    inline const Pos & get_caption_pos() { return caption_pos; }

    void compute_caption_dim() {
      if (form_entry.caption != nullptr) {
        font.get_size(form_entry.caption, &caption_dim, FORM_FONT_SIZE);
      }
      else {
        caption_dim = Dim(0, 0);
      }
    }

    virtual bool form_refresh_required() { return false; }
    virtual void             paint(Page::Format & fmt)            = 0;
    virtual void compute_field_dim()                              = 0;
    virtual void compute_field_pos(Pos from_pos)                  = 0;
    virtual void  update_highlight()                              = 0;
    virtual void        save_value()                              = 0;

    virtual bool  event(const EventMgr::Event & event) { return false; }

    inline bool in_event_control() { return event_control; }

    void compute_caption_pos(Pos from_pos) {
      caption_pos = Pos(from_pos.x - caption_dim.width, from_pos.y);
    }

    void show_highlighted(bool show_it) {
      if (show_it) {
        page.put_highlight(Dim(field_dim.width + 20, field_dim.height + 20),
                           Pos(field_pos.x     - 10, field_pos.y      - 10));
      }
      else {
        page.clear_highlight(Dim(field_dim.width + 20, field_dim.height + 20),
                             Pos(field_pos.x     - 10, field_pos.y      - 10));
      }
    }

    void show_selected(bool show_it) {
      if (show_it) {
        page.put_highlight(Dim(field_dim.width + 20, field_dim.height + 20),
                           Pos(field_pos.x     - 10, field_pos.y      - 10));
        page.put_highlight(Dim(field_dim.width + 22, field_dim.height + 22),
                           Pos(field_pos.x     - 11, field_pos.y      - 11));
        page.put_highlight(Dim(field_dim.width + 24, field_dim.height + 24),
                           Pos(field_pos.x     - 12, field_pos.y      - 12));
      }
      else {
        page.clear_highlight(Dim(field_dim.width + 20, field_dim.height + 20),
                             Pos(field_pos.x     - 10, field_pos.y      - 10));
        page.clear_highlight(Dim(field_dim.width + 22, field_dim.height + 22),
                             Pos(field_pos.x     - 11, field_pos.y      - 11));
        page.clear_highlight(Dim(field_dim.width + 24, field_dim.height + 24),
                             Pos(field_pos.x     - 12, field_pos.y      - 12));
      }
    }

    #if (INKPLATE_6PLUS || TOUCH_TRIAL)
      inline bool is_pointed(uint16_t x, uint16_t y) {
        return (x >= (field_pos.x - 10)) && 
               (y >= (field_pos.y - 10)) &&
               (x <= (field_pos.x + field_dim.width  + 10)) &&
               (y <= (field_pos.y + field_dim.height + 10));
      } 

    #endif

  protected:
    
    FormEntry & form_entry;
    Font      & font;
    Dim         field_dim, caption_dim;
    Pos         field_pos, caption_pos;
    bool        event_control;

  public:

    #if defined(BOARD_TYPE_PAPER_S3)
      static const char * fit_text_ellipsis(Font & font,
                                           const char * src,
                                           char * buf,
                                           size_t buf_size,
                                           int16_t max_w,
                                           uint8_t font_size)
      {
        if ((buf == nullptr) || (buf_size == 0)) return src;
        if (src == nullptr) {
          buf[0] = 0;
          return buf;
        }

        Dim dim;
        font.get_size(src, &dim, font_size);
        if (dim.width <= max_w) return src;
        if (buf_size < 4) return src;

        size_t n = strlen(src);
        if (n > (buf_size - 1)) n = (buf_size - 1);

        for (; n > 0; --n) {
          (void)snprintf(buf, buf_size, "%.*s...", (int)n, src);
          font.get_size(buf, &dim, font_size);
          if (dim.width <= max_w) return buf;
        }

        (void)snprintf(buf, buf_size, "...");
        return buf;
      }
    #endif
};

class FormChoiceField : public FormField
{
  public:
    static constexpr FormChoice dir_view_choices[2] = {
      { "Shelf",      0 },
      { "MATRIX",     1 }
    };

    static constexpr FormChoice sleep_screen_choices[2] = {
      { "COVER",      0 },
      { "RANDOM",     1 }
    };

    static constexpr FormChoice ok_cancel_choices[2] = {
      { "OK",         1 },
      { "CANCEL",     0 }
    };

    static constexpr FormChoice yes_no_choices[2] = {
      { "YES",        1 },
      { "NO",         0 }
    };

    static constexpr FormChoice resolution_choices[2] = {
      { "1Bit",       0 },
      { "3Bits",      1 }
    };

    static constexpr FormChoice timeout_choices[3] = {
      { "5",          5 },
      { "15",        15 },
      { "30",        30 }
    };

    static constexpr FormChoice battery_visual_choices[4] = {
      { "NONE",       0 },
      { "PERCENT",    1 },
      { "VOLTAGE",    2 },
      { "ICON",       3 }
    };

    static constexpr FormChoice font_size_choices[4] = {
      { "8",          8 },
      { "10",        10 },
      { "12",        12 },
      { "15",        15 }
    };

    #if DATE_TIME_RTC
      static constexpr FormChoice right_corner_choices[3] = {
        { "NONE",        0 },
        { "DATE TIME",   1 },
        { "HEAP INFO",   2 }
      };
    #endif

    #if (INKPLATE_6PLUS || TOUCH_TRIAL)
      static constexpr FormChoice orientation_choices[4] = {
        { "LEFT",     3 },
        { "RIGHT",    2 },
        { "TOP",      1 },
        { "BOTTOM",   0 }
      };
    #else
      static constexpr FormChoice orientation_choices[3] = {
        { "LEFT",     0 },
        { "RIGHT",    1 },
        { "BOTTOM",   2 }
      };
    #endif

    static FormChoice  font_choices[8];
    static uint8_t font_choices_count;

    void compute_field_dim() {
      field_dim = { 0, 0 };
      for (int8_t i = 0; i < form_entry.u.ch.choice_count; i++) {
        Item * item = item_pool.newElement();
        items.push_back(item);
        font.get_size(form_entry.u.ch.choices[i].caption, &item->dim, FORM_FONT_SIZE);
        item->idx = i;
      }

      int i = 0;
      current_item = items.end();
      for (Items::iterator it = items.begin(); it != items.end(); it++) {
        if (form_entry.u.ch.choices[i].value == * form_entry.u.ch.value) {
          current_item = it;
          break;
        }
        i++;        
      }
      if (current_item == items.end()) current_item = items.begin();

      old_item = items.end();
    }

    static void adjust_font_choices(char ** font_names, uint8_t size) {
      for (uint8_t i = 0; i < size; i++) font_choices[i].caption = font_names[i];
      font_choices_count = size; 
    }

    void compute_field_pos(Pos from_pos) = 0;

    void paint(Page::Format & fmt) {

      Font::Glyph * glyph  =  font.get_glyph('M', FORM_FONT_SIZE);
      uint8_t       offset = -glyph->yoff;
     
      page.put_str_at(form_entry.caption, 
                      Pos(caption_pos.x, caption_pos.y + offset), 
                      fmt);
      for (auto * item : items) {
        page.put_str_at(form_entry.u.ch.choices[item->idx].caption, 
                        Pos(item->pos.x, item->pos.y + offset), 
                        fmt);
      }
    }

    #if (INKPLATE_6PLUS || TOUCH_TRIAL)
      bool event(const EventMgr::Event & event) {
        old_item = current_item;
        Items::iterator it;
        for (it = items.begin(); it != items.end(); it++) {
          if ((event.x >= (*it)->pos.x - 5) && 
              (event.y >= (*it)->pos.y - 5) && 
              (event.x <= ((*it)->pos.x + (*it)->dim.width  + 5)) &&
              (event.y <= ((*it)->pos.y + (*it)->dim.height + 5))) {
            break;
          }
        }
        if (it != items.end()) current_item = it;
        return false;
      }
    #else
      bool event(const EventMgr::Event & event) {
        old_item = current_item;
        switch (event.kind) {
          case EventMgr::EventKind::DBL_PREV:
          case EventMgr::EventKind::PREV:
            if (current_item == items.begin()) current_item = items.end();
            current_item--;
            break;
          case EventMgr::EventKind::DBL_NEXT:
          case EventMgr::EventKind::NEXT:
            current_item++;
            if (current_item == items.end()) current_item = items.begin();
            break;
          default:
            break;
        }
        return false;
      }
    #endif

    void update_highlight() {
      if (old_item != current_item) {
        if (old_item != items.end()) {
          page.clear_highlight(Dim((*old_item)->dim.width + 10, (*old_item)->dim.height + 10),
                               Pos((*old_item)->pos.x     -  5, (*old_item)->pos.y      -  5));
        }
        page.put_highlight(Dim((*current_item)->dim.width + 10, (*current_item)->dim.height + 10),
                           Pos((*current_item)->pos.x     -  5, (*current_item)->pos.y      -  5));
      }
      old_item = current_item;
    }

    void save_value() {
      * form_entry.u.ch.value = form_entry.u.ch.choices[(*current_item)->idx].value; 
    }

  protected:
    struct Item {
      Pos     pos;
      Dim     dim;
      uint8_t idx;
    };

    static MemoryPool<Item> item_pool; // Shared by all FormChoiceField

    typedef std::list<Item *> Items;
    
    Items items;
    Items::iterator current_item, old_item;


  public:
    using FormField::FormField;

   ~FormChoiceField() {
      for (auto * item : items) {
        item_pool.deleteElement(item);
      }
      items.clear();
    }
};

class VFormChoiceField : public FormChoiceField
{
  private:
    static constexpr char const * TAG = "VFormChoiceField";

  public:
    using FormChoiceField::FormChoiceField;

   ~VFormChoiceField() { }

    void compute_field_pos(Pos from_pos) {
      field_pos   = from_pos; 
      Pos the_pos = from_pos;

      uint8_t line_height = font.get_line_height(FORM_FONT_SIZE);
      for (auto * item : items) {
        item->pos  = the_pos;
        the_pos.y += line_height;
        LOG_D("Item position  [%d, %d]", item->pos.x, item->pos.y);
      } 
    }
    
    void compute_field_dim() {
      FormChoiceField::compute_field_dim();
      uint8_t line_height = font.get_line_height(FORM_FONT_SIZE);
      uint8_t last_height = 0;
      for (auto * item : items) {
        if (field_dim.width < item->dim.width) field_dim.width = item->dim.width;
        field_dim.height += line_height;
        last_height       = item->dim.height;
        LOG_D("Item dimension: [%d, %d]", item->dim.width, item->dim.height);
      }
      field_dim.height += last_height - line_height;
    }
};

class HFormChoiceField : public FormChoiceField
{
  private:
    static constexpr char const * TAG = "HFormChoiceField";

  public:
    using FormChoiceField::FormChoiceField;

   ~HFormChoiceField() { }

    void compute_field_pos(Pos from_pos) { 
      field_pos   = from_pos; 
      Pos the_pos = from_pos;

      for (auto * item : items) {
        item->pos  = the_pos;
        the_pos.x += item->dim.width + 20;
        LOG_D("Item position: [%d, %d]", item->pos.x, item->pos.y);
      }
    }
    
    static constexpr uint8_t HORIZONTAL_SEPARATOR = 20;

    void compute_field_dim() {
      FormChoiceField::compute_field_dim();
      uint16_t separator = 0;
      for (auto * item : items) {
        if (field_dim.height < item->dim.height) field_dim.height = item->dim.height;
        field_dim.width += item->dim.width + separator;
        separator = HORIZONTAL_SEPARATOR;
        LOG_D("Item dimension: [%d, %d]", item->dim.width, item->dim.height);
      }
    }
};

#if defined(BOARD_TYPE_PAPER_S3)
class PaperS3ArrowChoiceField : public FormChoiceField
{
  public:
    using FormChoiceField::FormChoiceField;

    bool form_refresh_required() override {
      bool ret = changed;
      changed = false;
      return ret;
    }

    void compute_field_dim() override {
      FormChoiceField::compute_field_dim();

      const int16_t w = (int16_t)(Screen::get_width() - (PAPER_MARGIN_X * 2));
      uint8_t label_h = font.get_line_height(FORM_LABEL_FONT_SIZE);
      if (label_h < 10) label_h = 10;
      field_dim.width  = w;
      field_dim.height = (int16_t)(PAPER_PAD_Y + label_h + PAPER_PAD_Y + SELECTOR_H + PAPER_PAD_Y);
    }

    void compute_field_pos(Pos from_pos) override {
      field_pos = from_pos;

      uint8_t label_h = font.get_line_height(FORM_LABEL_FONT_SIZE);
      if (label_h < 10) label_h = 10;

      const int16_t selector_y = (int16_t)(field_pos.y + PAPER_PAD_Y + label_h + PAPER_PAD_Y);

      selector_pos = Pos((int16_t)(field_pos.x + PAPER_PAD_X), selector_y);
      selector_dim = Dim((int16_t)(field_dim.width - (PAPER_PAD_X * 2)), SELECTOR_H);

      arrow_dim = Dim(SELECTOR_H, SELECTOR_H);
      left_arrow_pos  = selector_pos;
      right_arrow_pos = Pos((int16_t)(selector_pos.x + selector_dim.width - arrow_dim.width), selector_pos.y);

      value_pos = Pos((int16_t)(left_arrow_pos.x + arrow_dim.width), selector_pos.y);
      value_dim = Dim((int16_t)(selector_dim.width - (arrow_dim.width * 2)), SELECTOR_H);
    }

    void paint(Page::Format & fmt) override {
      Page::Format label_fmt = fmt;
      label_fmt.font_size = FORM_LABEL_FONT_SIZE;

      Page::Format value_fmt = fmt;
      value_fmt.font_size = FORM_VALUE_FONT_SIZE;

      Font::Glyph * glyph = font.get_glyph('M', FORM_LABEL_FONT_SIZE);
      int16_t offset = 0;
      if (glyph != nullptr) offset = -glyph->yoff;

      page.put_rounded(field_dim, field_pos);
      const int16_t max_label_w = (int16_t)(field_dim.width - (PAPER_PAD_X * 2));
      char label_buf[96];
      const char * label = FormField::fit_text_ellipsis(font, form_entry.caption, label_buf, sizeof(label_buf), max_label_w, FORM_LABEL_FONT_SIZE);
      page.put_str_at(label, Pos((int16_t)(field_pos.x + PAPER_PAD_X), (int16_t)(field_pos.y + PAPER_PAD_Y + offset)), label_fmt);

      page.put_rounded(selector_dim, selector_pos);
      page.put_rounded(arrow_dim, left_arrow_pos);
      page.put_rounded(arrow_dim, right_arrow_pos);
      page.put_rounded(value_dim, value_pos);

      Dim d;
      font.get_size("<", &d, FORM_VALUE_FONT_SIZE);
      page.put_str_at("<", Pos((int16_t)(left_arrow_pos.x + (arrow_dim.width >> 1) - (d.width >> 1)), (int16_t)(left_arrow_pos.y + (arrow_dim.height >> 1) + (d.height >> 1))), value_fmt);
      font.get_size(">", &d, FORM_VALUE_FONT_SIZE);
      page.put_str_at(">", Pos((int16_t)(right_arrow_pos.x + (arrow_dim.width >> 1) - (d.width >> 1)), (int16_t)(right_arrow_pos.y + (arrow_dim.height >> 1) + (d.height >> 1))), value_fmt);

      const char * value = form_entry.u.ch.choices[(*current_item)->idx].caption;
      const int16_t max_value_w = (int16_t)(value_dim.width - 10);
      char value_buf[64];
      value = FormField::fit_text_ellipsis(font, value, value_buf, sizeof(value_buf), max_value_w, FORM_VALUE_FONT_SIZE);
      font.get_size(value, &d, FORM_VALUE_FONT_SIZE);
      page.put_str_at(value, Pos((int16_t)(value_pos.x + (value_dim.width >> 1) - (d.width >> 1)), (int16_t)(value_pos.y + (value_dim.height >> 1) + (d.height >> 1))), value_fmt);
    }

    bool event(const EventMgr::Event & event) override {
      if (items.empty()) return false;

      changed = false;

      if ((event.x >= left_arrow_pos.x) && (event.y >= left_arrow_pos.y) &&
          (event.x <= (uint16_t)(left_arrow_pos.x + arrow_dim.width)) && (event.y <= (uint16_t)(left_arrow_pos.y + arrow_dim.height))) {
        if (current_item == items.begin()) current_item = items.end();
        current_item--;
        changed = true;
      }
      else if ((event.x >= right_arrow_pos.x) && (event.y >= right_arrow_pos.y) &&
               (event.x <= (uint16_t)(right_arrow_pos.x + arrow_dim.width)) && (event.y <= (uint16_t)(right_arrow_pos.y + arrow_dim.height))) {
        current_item++;
        if (current_item == items.end()) current_item = items.begin();
        changed = true;
      }
      return false;
    }

    void update_highlight() override {
      page.put_rounded(Dim((int16_t)(value_dim.width + 4), (int16_t)(value_dim.height + 4)), Pos((int16_t)(value_pos.x - 2), (int16_t)(value_pos.y - 2)));
      page.put_rounded(Dim((int16_t)(value_dim.width + 6), (int16_t)(value_dim.height + 6)), Pos((int16_t)(value_pos.x - 3), (int16_t)(value_pos.y - 3)));
    }

    void save_value() override {
      * form_entry.u.ch.value = form_entry.u.ch.choices[(*current_item)->idx].value;
    }

  private:
    static constexpr int16_t PAPER_MARGIN_X = 24;
    static constexpr int16_t PAPER_PAD_X = 14;
    static constexpr int16_t PAPER_PAD_Y = 5;
    static constexpr int16_t SELECTOR_H = 38;

    Pos selector_pos;
    Dim selector_dim;
    Pos left_arrow_pos;
    Pos right_arrow_pos;
    Pos value_pos;
    Dim value_dim;
    Dim arrow_dim;

    bool changed = false;
};
#endif

class FormUInt16 : public FormField
{

  public:
    using FormField::FormField;

    bool form_refresh_required() { return true; }

    void compute_field_pos(Pos from_pos) { 
      field_pos = from_pos;

      #if defined(BOARD_TYPE_PAPER_S3)
        uint8_t label_h = font.get_line_height(FORM_LABEL_FONT_SIZE);
        if (label_h < 10) label_h = 10;
        box_pos = Pos((int16_t)(field_pos.x + PAPER_PAD_X), (int16_t)(field_pos.y + PAPER_PAD_Y + label_h + PAPER_PAD_Y));
      #endif
    }

    void paint(Page::Format & fmt) {
      char val[8];
      #if !defined(BOARD_TYPE_PAPER_S3)
        Font::Glyph * glyph  =  font.get_glyph('M', FORM_FONT_SIZE);
        uint8_t       offset = -glyph->yoff;
      #endif

      uint16_t v = * form_entry.u.val.value;
      if (v < form_entry.u.val.min) {
        v = form_entry.u.val.min;
      }
      else if (v > form_entry.u.val.max) {
        v = form_entry.u.val.max;
      }
      * form_entry.u.val.value = v;

      int_to_str(v, val, 8);

      #if defined(BOARD_TYPE_PAPER_S3)
        page.put_rounded(field_dim, field_pos);
        Page::Format label_fmt = fmt;
        label_fmt.font_size = FORM_LABEL_FONT_SIZE;
        Font::Glyph * label_glyph = font.get_glyph('M', FORM_LABEL_FONT_SIZE);
        int16_t label_offset = 0;
        if (label_glyph != nullptr) label_offset = -label_glyph->yoff;
        const int16_t max_label_w = (int16_t)(field_dim.width - (PAPER_PAD_X * 2));
        char label_buf[96];
        const char * label = FormField::fit_text_ellipsis(font, form_entry.caption, label_buf, sizeof(label_buf), max_label_w, FORM_LABEL_FONT_SIZE);
        page.put_str_at(label,
                        Pos((int16_t)(field_pos.x + PAPER_PAD_X), (int16_t)(field_pos.y + PAPER_PAD_Y + label_offset)),
                        label_fmt);

        page.put_rounded(box_dim, box_pos);
        Dim d;
        Page::Format value_fmt = fmt;
        value_fmt.font_size = FORM_VALUE_FONT_SIZE;
        font.get_size(val, &d, FORM_VALUE_FONT_SIZE);
        page.put_str_at(val,
                        Pos((int16_t)(box_pos.x + (box_dim.width >> 1) - (d.width >> 1)), (int16_t)(box_pos.y + (box_dim.height >> 1) + (d.height >> 1))),
                        value_fmt);
      #else
        page.put_str_at(form_entry.caption, 
                        Pos(caption_pos.x, caption_pos.y + offset), 
                        fmt);
        page.put_str_at(val, 
                        Pos(field_pos.x, field_pos.y + offset), 
                        fmt);
      #endif
   }

    bool event(const EventMgr::Event & event) {
      if (!event_control) {
        keypad_viewer.show(* form_entry.u.val.value, form_entry.caption);
        event_control = true;
      }
      else {
        if (!keypad_viewer.event(event)) {
          uint16_t v = keypad_viewer.get_value();
          if (v < form_entry.u.val.min) {
            v = form_entry.u.val.min;
          }
          else if (v > form_entry.u.val.max) {
            v = form_entry.u.val.max;
          }
          * form_entry.u.val.value = v;
          event_control = false;
          
          return false; // release events control
        }
      }
      return true; // keep events control
    }

    void update_highlight() {
      #if defined(BOARD_TYPE_PAPER_S3)
        page.put_rounded(Dim((int16_t)(box_dim.width + 4), (int16_t)(box_dim.height + 4)), Pos((int16_t)(box_pos.x - 2), (int16_t)(box_pos.y - 2)));
      #endif
    }

    void save_value() {
    }

    void compute_field_dim() {
      #if defined(BOARD_TYPE_PAPER_S3)
        const int16_t w = (int16_t)(Screen::get_width() - (PAPER_MARGIN_X * 2));
        uint8_t label_h = font.get_line_height(FORM_LABEL_FONT_SIZE);
        if (label_h < 10) label_h = 10;
        field_dim.width  = w;
        field_dim.height = (int16_t)(PAPER_PAD_Y + label_h + PAPER_PAD_Y + BOX_H + PAPER_PAD_Y);
        box_dim = Dim((int16_t)(field_dim.width - (PAPER_PAD_X * 2)), BOX_H);
      #else
        font.get_size("XXXXX", &field_dim, FORM_FONT_SIZE);
      #endif
    }

  #if defined(BOARD_TYPE_PAPER_S3)
  private:
    static constexpr int16_t PAPER_MARGIN_X = 24;
    static constexpr int16_t PAPER_PAD_X = 14;
    static constexpr int16_t PAPER_PAD_Y = 5;
    static constexpr int16_t BOX_H = 38;

    Pos box_pos;
    Dim box_dim;
  #endif
};

#if INKPLATE_6PLUS || TOUCH_TRIAL
class FormDone : public FormField
{
  public:
    using FormField::FormField;

    bool event(const EventMgr::Event & event);

    const Dim get_field_dim() {
      #if defined(BOARD_TYPE_PAPER_S3)
        return field_dim;
      #else
        return Dim(field_dim.width, field_dim.height + 10);
      #endif
    }
    void save_value() { }
    void update_highlight() { 
      page.put_rounded(
        Dim(field_dim.width  + 16,
            field_dim.height + 16),
        Pos(field_pos.x - 8, field_pos.y - 8));
      page.put_rounded(
        Dim(field_dim.width  + 18,
            field_dim.height + 18),
        Pos(field_pos.x - 9, field_pos.y - 9));
      page.put_rounded(
        Dim(field_dim.width  + 20,
            field_dim.height + 20),
        Pos(field_pos.x - 10, field_pos.y - 10));
    }

    void compute_field_dim() {
      #if defined(BOARD_TYPE_PAPER_S3)
        field_dim.width  = (int16_t)(Screen::get_width() - (PAPER_MARGIN_X * 2));
        field_dim.height = BUTTON_H;
      #else
        font.get_size(form_entry.caption, &field_dim, FORM_FONT_SIZE);
      #endif
    }

    void compute_field_pos(Pos from_pos) {
      #if defined(BOARD_TYPE_PAPER_S3)
        field_pos = from_pos;
      #else
        field_pos.x = (Screen::get_width() / 2) - (field_dim.width / 2);
        field_pos.y = from_pos.y + 10;
      #endif
    }

    void paint(Page::Format & fmt) {
      #if defined(BOARD_TYPE_PAPER_S3)
        page.put_rounded(field_dim, field_pos);
        Dim d;
        font.get_size(form_entry.caption, &d, FORM_FONT_SIZE);
        page.put_str_at(form_entry.caption,
                        Pos((int16_t)(field_pos.x + (field_dim.width >> 1) - (d.width >> 1)), (int16_t)(field_pos.y + (field_dim.height >> 1) + (d.height >> 1))),
                        fmt);
      #else
        Font::Glyph * glyph  =  font.get_glyph('M', FORM_FONT_SIZE);
        uint8_t       offset = -glyph->yoff;
        page.put_str_at(form_entry.caption, 
                        Pos(field_pos.x, field_pos.y + offset), 
                        fmt);
      #endif
    }

  #if defined(BOARD_TYPE_PAPER_S3)
  private:
    static constexpr int16_t PAPER_MARGIN_X = 24;
    static constexpr int16_t BUTTON_H = 54;
  #endif
};
#endif

class FieldFactory
{
  public:
    static FormField * create(FormEntry & entry, Font & font) {
      switch (entry.entry_type) {
        case FormEntryType::HORIZONTAL:
          #if defined(BOARD_TYPE_PAPER_S3)
            return new PaperS3ArrowChoiceField(entry, font);
          #else
            return new HFormChoiceField(entry, font);
          #endif
        case FormEntryType::VERTICAL:
          #if defined(BOARD_TYPE_PAPER_S3)
            return new PaperS3ArrowChoiceField(entry, font);
          #else
            if ((Screen::get_width() > Screen::get_height()) && (entry.u.ch.choice_count <= 4)) {
              return new HFormChoiceField(entry, font);
            }
            else {
              return new VFormChoiceField(entry, font);
            }
          #endif
        case FormEntryType::UINT16:
          return new FormUInt16(entry, font);
      #if INKPLATE_6PLUS || TOUCH_TRIAL
        case FormEntryType::DONE:
          return new FormDone(entry, font);
      #endif
      }
      return nullptr;
    }
};

class FormViewer
{
  public:
    typedef FormEntry * FormEntries;

  private:
    static constexpr char const * TAG = "FormViewer";

    #if defined(BOARD_TYPE_PAPER_S3)
      static constexpr uint8_t TOP_YPOS         =   0;
      static constexpr uint8_t BOTTOM_YPOS      =   0;
    #else
      static constexpr uint8_t TOP_YPOS         = 100;
      static constexpr uint8_t BOTTOM_YPOS      =  50;
    #endif

    FormEntries  form_entries;
    uint8_t      size;
    const char * bottom_msg;

    int16_t all_fields_width, all_captions_width;
    // int16_t last_choices_width;
    int8_t  line_height;
    bool    highlighting_field;
    bool    selecting_field;
    bool    completed;

    typedef std::list<FormField *> Fields;
    
    Fields fields;
    Fields::iterator current_field;

    Pos bottom_msg_pos;

    #if defined(BOARD_TYPE_PAPER_S3)
      uint8_t page_index = 0;
      uint8_t page_count = 1;
      std::vector<uint8_t> page_starts;
    #endif

    #if (INKPLATE_6PLUS || TOUCH_TRIAL)
      Fields::iterator find_field(uint16_t x, uint16_t y) {
        for (Fields::iterator it = fields.begin(); it != fields.end(); it++) {
          if ((*it)->is_pointed(x, y)) return it;
        }

        return fields.end();
      }

    #endif

  public:

    void set_completed(bool value) { completed = value; }

    void show(FormEntries _form_entries, int8_t _size, const char * _bottom_msg, bool refresh = false) {

      if (!refresh) {
        form_entries = _form_entries;
        size         = _size;
        bottom_msg   = _bottom_msg;

        Font * font =  fonts.get(1);

        for (auto * field : fields) delete field;
        fields.clear();

        for (int i = 0; i < size; i++) {
          FormField * field = FieldFactory::create(form_entries[i], *font); 
          if (field != nullptr) {
            fields.push_back(field);
            field->compute_caption_dim();
            field->compute_field_dim();
            LOG_D("Field dimentions: Caption: [%d, %d] Field: [%d, %d]", 
                  field->get_caption_dim().width, field->get_caption_dim().height,
                  field->get_field_dim().width, field->get_field_dim().height);
          }
        }

        all_fields_width = all_captions_width = 0;

        int16_t width;
        
        for (auto * field : fields) {
          width = field->get_field_dim().width;
          if (width > all_fields_width) all_fields_width = width;
          
          width = field->get_caption_dim().width;
          if (width > all_captions_width) all_captions_width = width;
        }

        #if !defined(BOARD_TYPE_PAPER_S3)
          width = all_captions_width + all_fields_width + 35;
          const int16_t right_xpos    = (Screen::get_width() >> 1) + (width >> 1);

          int16_t       current_ypos  = TOP_YPOS + 20;
          int16_t       caption_right = right_xpos - all_fields_width - 35;
          int16_t       field_left    = right_xpos - all_fields_width - 10;

          for (auto * field : fields) {
            field->compute_caption_pos(Pos(caption_right, current_ypos));
            field->compute_field_pos(Pos(field_left, current_ypos));
            current_ypos += field->get_field_dim().height + 20;
            LOG_D("Field positions: Caption: [%d, %d] Field: [%d, %d]", 
                  field->get_caption_pos().x, field->get_caption_pos().y,
                  field->get_field_pos().x, field->get_field_pos().y);
          }

          bottom_msg_pos = Pos(40, current_ypos + 30);
        #endif
      }

      #if defined(BOARD_TYPE_PAPER_S3)
        Font * font = fonts.get(1);
        int16_t msg_line_h = 14;
        if (font != nullptr) {
          msg_line_h = (int16_t)font->get_line_height(FORM_LABEL_FONT_SIZE);
          if (msg_line_h <= 0) msg_line_h = 14;
        }

        bottom_msg_pos = Pos(24, (int16_t)(Screen::get_height() - BOTTOM_YPOS - msg_line_h - 10));
        const int16_t content_bottom = (int16_t)(bottom_msg_pos.y - 20);

        if (!refresh) {
          page_index = 0;
        }

        static constexpr int16_t PAPER_MARGIN_X = 24;
        static constexpr int16_t FIELD_GAP_Y = 8;
        static constexpr int16_t START_Y = TOP_YPOS + 8;
        static constexpr int16_t CAPTION_INSET_X = 14;
        static constexpr int16_t CAPTION_INSET_Y = 10;

        page_starts.clear();
        page_starts.push_back(0);

        int16_t current_ypos = START_Y;
        uint8_t idx = 0;
        for (auto * field : fields) {
          const int16_t field_h = (int16_t)field->get_field_dim().height;
          if ((idx > 0) && (current_ypos != START_Y) && ((int16_t)(current_ypos + field_h) > content_bottom)) {
            page_starts.push_back(idx);
            current_ypos = START_Y;
          }
          current_ypos = (int16_t)(current_ypos + field_h + FIELD_GAP_Y);
          idx++;
        }

        page_count = (uint8_t)page_starts.size();
        if (page_count == 0) page_count = 1;
        if (page_index >= page_count) page_index = 0;

        const uint8_t total_fields_layout = (uint8_t)fields.size();
        const uint8_t start_idx_layout = page_starts[page_index];
        const uint8_t end_idx_layout = (uint8_t)((page_index + 1 < page_count) ? page_starts[page_index + 1] : total_fields_layout);

        current_ypos = START_Y;
        idx = 0;
        for (auto * field : fields) {
          if ((idx >= start_idx_layout) && (idx < end_idx_layout)) {
            const int16_t card_x = PAPER_MARGIN_X;
            field->compute_caption_pos(Pos((int16_t)(card_x + CAPTION_INSET_X + field->get_caption_dim().width), (int16_t)(current_ypos + CAPTION_INSET_Y)));
            field->compute_field_pos(Pos(card_x, current_ypos));
            current_ypos = (int16_t)(current_ypos + (int16_t)field->get_field_dim().height + FIELD_GAP_Y);
          }
          else {
            field->compute_caption_pos(Pos(30000, 30000));
            field->compute_field_pos(Pos(30000, 30000));
          }
          idx++;
        }
      #endif

      // Display the form

      Page::Format fmt = {
        .line_height_factor =   1.0,
        .font_index         =     1,
        .font_size          = FORM_FONT_SIZE,
        .indent             =     0,
        .margin_left        =     5,
        .margin_right       =     5,
        .margin_top         =     0,
        .margin_bottom      =     0,
        .screen_left        =    20,
        .screen_right       =    20,
        .screen_top         = TOP_YPOS,
        .screen_bottom      = BOTTOM_YPOS,
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

      // The large rectangle into which the form will be drawn

      #if defined(BOARD_TYPE_PAPER_S3)
        page.clear_region(Dim(Screen::get_width(), Screen::get_height()), Pos(0, 0));
      #else
        page.clear_region(
          Dim(Screen::get_width() - 40, Screen::get_height() - fmt.screen_bottom - fmt.screen_top),
          Pos(20, TOP_YPOS));

        page.put_highlight(
          Dim(Screen::get_width() - 44, Screen::get_height() - fmt.screen_bottom - fmt.screen_top - 4),
          Pos(22, TOP_YPOS + 2));
      #endif

      // Show all captions (but the last one (OK / CANCEL) or (DONE)) and choices 

      #if defined(BOARD_TYPE_PAPER_S3)
        const uint8_t total_fields_paint = (uint8_t)fields.size();
        const uint8_t start_idx_paint = (page_count > 0) ? page_starts[page_index] : 0;
        const uint8_t end_idx_paint = (uint8_t)((page_count > 0 && (page_index + 1 < page_count)) ? page_starts[page_index + 1] : total_fields_paint);

        uint8_t paint_idx = 0;
        for (auto * field : fields) {
          if ((paint_idx >= start_idx_paint) && (paint_idx < end_idx_paint)) {
            field->paint(fmt);
            field->update_highlight();
          }
          paint_idx++;
        }

        Page::Format bottom_fmt = fmt;
        bottom_fmt.font_size = FORM_LABEL_FONT_SIZE;

        char pbuf[12];
        int16_t reserved_right = 10;
        if (page_count > 1) {
          snprintf(pbuf, sizeof(pbuf), "%u/%u", (unsigned)(page_index + 1), (unsigned)page_count);
          Font * font = fonts.get(1);
          if (font != nullptr) {
            Dim dim;
            font->get_size(pbuf, &dim, FORM_LABEL_FONT_SIZE);
            const int16_t px = (int16_t)(Screen::get_width() - 24 - dim.width);
            reserved_right = (int16_t)(dim.width + 24 + 10);
            page.put_str_at(pbuf, Pos(px, bottom_msg_pos.y), bottom_fmt);
          }
        }

        const int16_t max_bottom_w = (int16_t)(Screen::get_width() - bottom_msg_pos.x - reserved_right);
        Font * bottom_font = fonts.get(1);
        if (bottom_font != nullptr) {
          char bottom_buf[160];
          const char * bottom_str = FormField::fit_text_ellipsis(*bottom_font, bottom_msg, bottom_buf, sizeof(bottom_buf), max_bottom_w, FORM_LABEL_FONT_SIZE);
          page.put_str_at(bottom_str, bottom_msg_pos, bottom_fmt);
        }
      #else
        for (auto * field : fields) {
          field->paint(fmt);
          field->update_highlight();
        }

        page.put_str_at(bottom_msg, bottom_msg_pos, fmt);
      #endif

      if (!refresh) {
        selecting_field = false;

        #if (INKPLATE_6PLUS || TOUCH_TRIAL)
          current_field = fields.end();
          highlighting_field = false;
        #else
          current_field = fields.begin();
          highlighting_field = true;
          (*current_field)->show_highlighted(true);
        #endif
      }

      #if !defined(BOARD_TYPE_PAPER_S3)
        ScreenBottom::show();
      #endif

      page.paint(false);
    }

    bool event(const EventMgr::Event & event) {

      completed = false;
      
      #if (INKPLATE_6PLUS || TOUCH_TRIAL)
        if (current_field != fields.end()) {
          if (!(*current_field)->event(event)) {
            // The field releases control of future events
            // Redraw the form
            show(form_entries, size, bottom_msg, true);
            current_field = fields.end();
          }
          return false; // Not completed yet
        }
        else {
          switch (event.kind) {
            #if defined(BOARD_TYPE_PAPER_S3)
              case EventMgr::EventKind::SWIPE_LEFT:
                if (page_count > 1) {
                  page_index = (uint8_t)((page_index + 1) % page_count);
                  show(form_entries, size, bottom_msg, true);
                }
                return false;
              case EventMgr::EventKind::SWIPE_RIGHT:
                if (page_count > 1) {
                  page_index = (uint8_t)((page_index == 0) ? (page_count - 1) : (page_index - 1));
                  show(form_entries, size, bottom_msg, true);
                }
                return false;
            #endif
            case EventMgr::EventKind::TAP:
              current_field = find_field(event.x, event.y);

              if (current_field != fields.end()) {
                if ((*current_field)->event(event)) {
                  // The field needs to keep control of future events
                  // current_field is not reset for next event loop to pass
                  // control to it.
                  return false;
                }
                else {
                  if ((*current_field)->form_refresh_required()) {
                    show(form_entries, size, bottom_msg, true);
                  }
                  // The field doesn't need control of future events
                  current_field = fields.end();
                }
              }
              break;

            default:
              break;
          }
        }
      #else
        Fields::iterator old_field = current_field;
        if (highlighting_field) {
          switch (event.kind) {
            case EventMgr::EventKind::DBL_PREV:
            case EventMgr::EventKind::PREV:
              if (current_field == fields.begin()) current_field = fields.end();
              current_field--;
              break;
            case EventMgr::EventKind::DBL_NEXT:
            case EventMgr::EventKind::NEXT:
              current_field++;
              if (current_field == fields.end()) current_field = fields.begin();
              break;
            case EventMgr::EventKind::SELECT:
              highlighting_field = false;
              selecting_field = true;
              break;
            case EventMgr::EventKind::NONE:
              return false;
            case EventMgr::EventKind::DBL_SELECT:
              completed = true;
              break;
          }
        }
        else {
          bool was_in_control = (*current_field)->in_event_control();
          if (!(*current_field)->event(event)) {
            if ((*current_field)->form_refresh_required()) {
              show(form_entries, size, bottom_msg, true);
            }
            switch (event.kind) {
              case EventMgr::EventKind::SELECT:
                highlighting_field = true;
                old_field = current_field;
                current_field++;
                if (current_field == fields.end()) current_field = fields.begin();
                break;
              case EventMgr::EventKind::NONE:
                return false;
              case EventMgr::EventKind::DBL_SELECT:
                if (!was_in_control) completed = true;
                else {
                  highlighting_field = true;
                  old_field = current_field;
                  current_field++;
                  if (current_field == fields.end()) current_field = fields.begin();
                }
                break;
              default:
                break;
            }
          }
        }
      #endif
      
      Page::Format fmt = {
        .line_height_factor = 1.0,
        .font_index         =   1,
        .font_size          = FORM_FONT_SIZE,
        .indent             =   0,
        .margin_left        =   0,
        .margin_right       =   0,
        .margin_top         =   0,
        .margin_bottom      =   0,
        .screen_left        =  20,
        .screen_right       =  20,
        .screen_top         =  TOP_YPOS,
        .screen_bottom      =  BOTTOM_YPOS,
        .width              =   0,
        .height             =   0,
        .vertical_align     =   0,
        .trim               = true,
        .pre                = false,
        .font_style         = Fonts::FaceStyle::NORMAL,
        .align              = CSS::Align::LEFT,
        .text_transform     = CSS::TextTransform::NONE,
        .display            = CSS::Display::INLINE
      };

      page.start(fmt);

      if (completed) {
        for (auto * field : fields) {
          field->save_value();
          delete field;
        }
        fields.clear();

        #if defined(BOARD_TYPE_PAPER_S3)
          page.clear_region(Dim(Screen::get_width(), Screen::get_height()), Pos(0, 0));
        #else
          page.clear_region(
            Dim(Screen::get_width() - 40, Screen::get_height() - fmt.screen_bottom - fmt.screen_top),
            Pos(20, TOP_YPOS));
        #endif

        page.paint(false);
      }
      else {
        if (highlighting_field) {
          #if !(INKPLATE_6PLUS || TOUCH_TRIAL)
            (*old_field)->show_selected(false);
            (*current_field)->show_highlighted(true);
          #endif
        }
        else {
          #if !(INKPLATE_6PLUS || TOUCH_TRIAL)
            if (selecting_field) {
              selecting_field = false;
              (*current_field)->show_selected(true);
              (*current_field)->event(event);
            }
          #endif

          for (auto * field : fields) {
            field->update_highlight();
          }
        }

        #if !defined(BOARD_TYPE_PAPER_S3)
          ScreenBottom::show();
        #endif
        
        page.paint(false);
      }
      
      return completed;
    }
};

#if __FORM_VIEWER__
  FormViewer form_viewer;
#else
  extern FormViewer form_viewer;
#endif
