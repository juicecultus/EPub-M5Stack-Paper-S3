// Paper S3 EventMgr stub implementation
// Provides a minimal, no-input EventMgr so the EPUB app can run
// on BOARD_TYPE_PAPER_S3 without Inkplate-specific key handling.

#include "global.hpp"

#if defined(BOARD_TYPE_PAPER_S3)

#include "controllers/event_mgr.hpp"
#include "controllers/app_controller.hpp"
#include "controllers/common_actions.hpp"
#include "models/config.hpp"
#include "screen.hpp"

#if EPUB_INKPLATE_BUILD
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "freertos/queue.h"
  #include "driver/i2c_master.h"
  #include "esp_log.h"
  #include "inkplate_platform.hpp"
  #include "esp.hpp"
#endif

EventMgr event_mgr;

#if EPUB_INKPLATE_BUILD

static constexpr char const * TAG = "EventMgrPaperS3";

static const gpio_num_t PAPERS3_GT911_SDA_GPIO = GPIO_NUM_41;
static const gpio_num_t PAPERS3_GT911_SCL_GPIO = GPIO_NUM_42;
static const i2c_port_num_t PAPERS3_GT911_I2C_PORT = I2C_NUM_1;

static uint8_t gt911_addr = 0x14;
static bool    gt911_ok   = false;

static uint16_t gt911_x_max = 0;
static uint16_t gt911_y_max = 0;

static i2c_master_bus_handle_t gt911_bus = nullptr;
static i2c_master_dev_handle_t gt911_dev_14 = nullptr;
static i2c_master_dev_handle_t gt911_dev_5d = nullptr;

static QueueHandle_t input_event_queue = nullptr;

static inline i2c_master_dev_handle_t gt911_handle_for_addr(uint8_t addr)
{
  if (addr == 0x14) return gt911_dev_14;
  if (addr == 0x5D) return gt911_dev_5d;
  return nullptr;
}

static esp_err_t gt911_write_reg(uint8_t addr, uint16_t reg, const uint8_t * data, size_t len)
{
  i2c_master_dev_handle_t dev = gt911_handle_for_addr(addr);
  if (dev == nullptr) return ESP_ERR_INVALID_STATE;

  uint8_t reg_hi = reg >> 8;
  uint8_t reg_lo = reg & 0xFF;

  uint8_t reg_buf[2] = { reg_hi, reg_lo };

  if ((data != nullptr) && (len != 0)) {
    i2c_master_transmit_multi_buffer_info_t buffers[2];
    buffers[0].write_buffer = reg_buf;
    buffers[0].buffer_size = sizeof(reg_buf);
    buffers[1].write_buffer = (uint8_t *)data;
    buffers[1].buffer_size = len;
    return i2c_master_multi_buffer_transmit(dev, buffers, 2, 100);
  }

  return i2c_master_transmit(dev, reg_buf, sizeof(reg_buf), 100);
}

static esp_err_t gt911_read_reg(uint8_t addr, uint16_t reg, uint8_t * data, size_t len)
{
  if ((data == nullptr) || (len == 0)) return ESP_ERR_INVALID_ARG;

  i2c_master_dev_handle_t dev = gt911_handle_for_addr(addr);
  if (dev == nullptr) return ESP_ERR_INVALID_STATE;

  uint8_t reg_hi = reg >> 8;
  uint8_t reg_lo = reg & 0xFF;

  uint8_t reg_buf[2] = { reg_hi, reg_lo };
  return i2c_master_transmit_receive(dev, reg_buf, sizeof(reg_buf), data, len, 100);
}

enum class Gt911ReadResult : uint8_t {
  UPDATED,    // New sample with at least one touch point.
  NO_TOUCH,   // New sample indicates no touch points (release).
  NO_UPDATE,  // No new sample available.
  ERROR,      // I2C / device error.
};

static Gt911ReadResult gt911_read_point(uint16_t * x, uint16_t * y)
{
  if (!gt911_ok || (x == nullptr) || (y == nullptr)) return Gt911ReadResult::ERROR;

  uint8_t status = 0;
  if (gt911_read_reg(gt911_addr, 0x814E, &status, 1) != ESP_OK) return Gt911ReadResult::ERROR;

  // The 0x80 bit indicates a new sample is ready. If it's not set, keep the
  // existing gesture state untouched (NO_UPDATE) to avoid false "release"
  // transitions which can register as double taps on short presses.
  if ((status & 0x80) == 0) return Gt911ReadResult::NO_UPDATE;

  uint8_t points = status & 0x0F;
  if (points == 0) {
    uint8_t zero = 0;
    gt911_write_reg(gt911_addr, 0x814E, &zero, 1);
    return Gt911ReadResult::NO_TOUCH;
  }

  uint8_t data[4] = { 0 };
  if (gt911_read_reg(gt911_addr, 0x8150, data, sizeof(data)) != ESP_OK) return Gt911ReadResult::ERROR;

  *x = (uint16_t)((data[1] << 8) | data[0]);
  *y = (uint16_t)((data[3] << 8) | data[2]);

  if (gt911_x_max != 0 && gt911_y_max != 0) {
    uint16_t sx = Screen::get_width();
    uint16_t sy = Screen::get_height();

    auto abs_u32 = [](int32_t v) -> uint32_t { return (v < 0) ? (uint32_t)(-v) : (uint32_t)v; };
    uint32_t diff_no_swap = abs_u32((int32_t)gt911_x_max - (int32_t)sx) + abs_u32((int32_t)gt911_y_max - (int32_t)sy);
    uint32_t diff_swap    = abs_u32((int32_t)gt911_x_max - (int32_t)sy) + abs_u32((int32_t)gt911_y_max - (int32_t)sx);
    bool swap = diff_swap < diff_no_swap;

    uint16_t raw_x = *x;
    uint16_t raw_y = *y;
    if (swap) {
      uint16_t tmp = raw_x;
      raw_x = raw_y;
      raw_y = tmp;
    }

    uint32_t x_den = (swap ? gt911_y_max : gt911_x_max);
    uint32_t y_den = (swap ? gt911_x_max : gt911_y_max);

    if (x_den > 1) {
      raw_x = (uint16_t)(((uint32_t)raw_x * (uint32_t)(sx - 1)) / (x_den - 1));
    }
    if (y_den > 1) {
      raw_y = (uint16_t)(((uint32_t)raw_y * (uint32_t)(sy - 1)) / (y_den - 1));
    }

    if (raw_x >= sx) raw_x = sx - 1;
    if (raw_y >= sy) raw_y = sy - 1;

    *x = raw_x;
    *y = raw_y;
  }

  uint8_t zero = 0;
  gt911_write_reg(gt911_addr, 0x814E, &zero, 1);

  return Gt911ReadResult::UPDATED;
}

static void touch_task(void * param)
{
  (void)param;

  // Simple gesture state machine inspired by Inkplate-6PLUS touch handling.
  // We interpret GT911 samples as a single-finger stream and classify each
  // interaction as a TAP, horizontal SWIPE_LEFT / SWIPE_RIGHT, or HOLD /
  // RELEASE. Coordinates are reported in the logical Screen space.

  constexpr uint16_t swipe_threshold          = 100; // pixels in GT911 space
  constexpr uint16_t longpress_move_threshold =  30; // max motion during hold
  constexpr uint32_t longpress_ms             = 600; // press duration
  constexpr TickType_t edge_repeat_ticks      = pdMS_TO_TICKS(500);

  bool       touch_active = false;
  bool       hold_sent    = false;
  bool       edge_repeat_sent    = false;
  EventMgr::EventKind edge_repeat_kind = EventMgr::EventKind::NONE;
  uint16_t   start_x      = 0;
  uint16_t   start_y      = 0;
  uint16_t   current_x    = 0;
  uint16_t   current_y    = 0;
  TickType_t start_tick   = 0;
  TickType_t last_repeat_tick = 0;

  while (true) {
    uint16_t x = 0;
    uint16_t y = 0;

    const Gt911ReadResult res = gt911_read_point(&x, &y);
    if (res == Gt911ReadResult::ERROR) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    auto maybe_send_hold_or_edge_repeat = [&]() {
      if (!touch_active) return;
      TickType_t now = xTaskGetTickCount();

      int dx = (int)current_x - (int)start_x;
      int dy = (int)current_y - (int)start_y;
      if (dx < 0) dx = -dx;
      if (dy < 0) dy = -dy;

      if ((dx > (int)longpress_move_threshold) || (dy > (int)longpress_move_threshold)) return;

      if ((edge_repeat_kind != EventMgr::EventKind::NONE) &&
          ((now - start_tick) >= edge_repeat_ticks) &&
          ((now - last_repeat_tick) >= edge_repeat_ticks)) {
        EventMgr::Event ev;
        ev.kind = edge_repeat_kind;
        ev.x    = start_x;
        ev.y    = start_y;
        ev.dist = 0;

        if (input_event_queue != nullptr) {
          xQueueSend(input_event_queue, &ev, 0);
        }

        ESP_LOGI(TAG, "Touch EDGE_REPEAT kind=%s x=%u y=%u",
                 (ev.kind == EventMgr::EventKind::SWIPE_RIGHT) ? "SWIPE_RIGHT" : "SWIPE_LEFT",
                 (unsigned)ev.x, (unsigned)ev.y);

        last_repeat_tick = now;
        edge_repeat_sent = true;
        return;
      }

      // Default: detect a long press while the finger is still down.
      if (hold_sent || edge_repeat_kind != EventMgr::EventKind::NONE) return;
      const uint32_t dt_ms = (now - start_tick) * portTICK_PERIOD_MS;
      if (dt_ms >= longpress_ms) {
        EventMgr::Event ev;
        ev.kind = EventMgr::EventKind::HOLD;
        ev.x    = start_x;
        ev.y    = start_y;
        ev.dist = 0;

        if (input_event_queue != nullptr) {
          xQueueSend(input_event_queue, &ev, 0);
        }

        ESP_LOGI(TAG, "Touch HOLD x=%u y=%u", (unsigned)ev.x, (unsigned)ev.y);

        hold_sent = true;
      }
    };

    // IMPORTANT: Don't interpret NO_UPDATE as \"no touch\". That would cause
    // false release/press transitions and can register as double taps.
    if (res == Gt911ReadResult::NO_UPDATE) {
      maybe_send_hold_or_edge_repeat();
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    const bool has_touch = (res == Gt911ReadResult::UPDATED);

    if (has_touch) {
      if (!touch_active) {
        // First contact
        touch_active = true;
        hold_sent    = false;
        edge_repeat_sent    = false;
        edge_repeat_kind    = EventMgr::EventKind::NONE;
        start_tick   = xTaskGetTickCount();
        last_repeat_tick = start_tick;
        start_x      = current_x = x;
        start_y      = current_y = y;

        const uint16_t w = Screen::get_width();
        if (start_x < (w / 3)) {
          edge_repeat_kind    = EventMgr::EventKind::SWIPE_RIGHT;
        }
        else if (start_x > ((w / 3) * 2)) {
          edge_repeat_kind    = EventMgr::EventKind::SWIPE_LEFT;
        }
      }
      else {
        // Update current finger position while it moves.
        current_x = x;
        current_y = y;
      }

      maybe_send_hold_or_edge_repeat();
    }
    else {
      if (touch_active) {
        // Touch has just ended – classify the gesture.
        touch_active = false;

        EventMgr::Event ev;
        ev.x    = start_x;
        ev.y    = start_y;
        ev.dist = 0;
        ev.kind = EventMgr::EventKind::NONE;

        TickType_t end_tick = xTaskGetTickCount();
        uint32_t   dt_ms    = (end_tick - start_tick) * portTICK_PERIOD_MS;

        int dx    = (int)current_x - (int)start_x;
        int dy    = (int)start_y   - (int)current_y; // positive when moving up
        int abs_dx = dx >= 0 ? dx : -dx;
        int abs_dy = dy >= 0 ? dy : -dy;

        (void)dt_ms; // dt_ms currently unused but kept for potential tuning.

        if (edge_repeat_sent) {
          // End of an edge-repeat sequence.
          ev.kind = EventMgr::EventKind::RELEASE;
          ESP_LOGI(TAG, "Touch RELEASE (edge-repeat) x=%u y=%u", (unsigned)ev.x, (unsigned)ev.y);
        }
        else if (hold_sent) {
          // End of a long-press sequence.
          ev.kind = EventMgr::EventKind::RELEASE;
          ESP_LOGI(TAG, "Touch RELEASE x=%u y=%u", (unsigned)ev.x, (unsigned)ev.y);
        }
        else if ((abs_dx > abs_dy) && (abs_dx > (int)swipe_threshold)) {
          // Horizontal swipe for page-level navigation.
          ev.kind = (dx > 0) ? EventMgr::EventKind::SWIPE_RIGHT
                             : EventMgr::EventKind::SWIPE_LEFT;
          ESP_LOGI(TAG, "Touch SWIPE x=%u y=%u dx=%d", (unsigned)ev.x, (unsigned)ev.y, dx);
        }
        else {
          // Short interaction: treat as a TAP.
          ev.kind = EventMgr::EventKind::TAP;
          ESP_LOGI(TAG, "Touch TAP x=%u y=%u", (unsigned)ev.x, (unsigned)ev.y);
        }

        if ((ev.kind != EventMgr::EventKind::NONE) && (input_event_queue != nullptr)) {
          xQueueSend(input_event_queue, &ev, 0);
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

#endif // EPUB_INKPLATE_BUILD

bool EventMgr::setup()
{
#if EPUB_INKPLATE_BUILD
  if (input_event_queue == nullptr) {
    input_event_queue = xQueueCreate(10, sizeof(Event));
  }

  i2c_master_bus_config_t bus_cfg = {};
  bus_cfg.i2c_port = PAPERS3_GT911_I2C_PORT;
  bus_cfg.sda_io_num = PAPERS3_GT911_SDA_GPIO;
  bus_cfg.scl_io_num = PAPERS3_GT911_SCL_GPIO;
  bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_cfg.glitch_ignore_cnt = 7;
  bus_cfg.intr_priority = 0;
  bus_cfg.trans_queue_depth = 0;
  bus_cfg.flags.enable_internal_pullup = 1;

  esp_err_t err = i2c_new_master_bus(&bus_cfg, &gt911_bus);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2c_new_master_bus failed: %d", (int)err);
  }
  else {
    uint8_t detected_addr = 0;
    if (i2c_master_probe(gt911_bus, 0x14, 100) == ESP_OK) {
      detected_addr = 0x14;
    }
    else if (i2c_master_probe(gt911_bus, 0x5D, 100) == ESP_OK) {
      detected_addr = 0x5D;
    }

    if (detected_addr == 0) {
      ESP_LOGE(TAG, "GT911 not found on I2C bus");
    }
    else {
      i2c_device_config_t dev_cfg = {};
      dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
      dev_cfg.device_address = detected_addr;
      dev_cfg.scl_speed_hz = 400000;
      dev_cfg.scl_wait_us = 0;
      dev_cfg.flags.disable_ack_check = 0;

      if (detected_addr == 0x14) {
        err = i2c_master_bus_add_device(gt911_bus, &dev_cfg, &gt911_dev_14);
      } else {
        err = i2c_master_bus_add_device(gt911_bus, &dev_cfg, &gt911_dev_5d);
      }

      if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %d", (int)err);
      }
      else {
        gt911_addr = detected_addr;
        gt911_ok = true;
        ESP_LOGI(TAG, "GT911 detected at 0x%02X", (unsigned)detected_addr);

        uint8_t cfg[4] = { 0 };
        if (gt911_read_reg(gt911_addr, 0x8048, cfg, sizeof(cfg)) == ESP_OK) {
          gt911_x_max = (uint16_t)((cfg[1] << 8) | cfg[0]);
          gt911_y_max = (uint16_t)((cfg[3] << 8) | cfg[2]);
          ESP_LOGI(TAG, "GT911 max: x=%u y=%u (screen %u x %u)", (unsigned)gt911_x_max, (unsigned)gt911_y_max,
                   (unsigned)Screen::get_width(), (unsigned)Screen::get_height());
        }
      }
    }
  }

  TaskHandle_t handle = nullptr;
  xTaskCreatePinnedToCore(touch_task, "papers3_touch", 4096, nullptr, 5, &handle, 1);
#endif

  return true;
}

void EventMgr::loop()
{
#if EPUB_INKPLATE_BUILD
  static uint32_t last_activity_ms = 0;
  if (last_activity_ms == 0) {
    last_activity_ms = (uint32_t)ESP::millis();
  }

  while (true) {
    const Event & event = get_event();

    if (event.kind != EventKind::NONE) {
      last_activity_ms = (uint32_t)ESP::millis();
      app_controller.input_event(event);
      return;
    }

    app_controller.input_event(event);

    if (!stay_on) {
      int8_t timeout_minutes = 0;
      config.get(Config::Ident::TIMEOUT, &timeout_minutes);
      if (timeout_minutes <= 0) {
        continue;
      }

      const uint32_t now_ms = (uint32_t)ESP::millis();
      const uint32_t timeout_ms = (uint32_t)timeout_minutes * 60U * 1000U;
      if ((now_ms - last_activity_ms) >= timeout_ms) {
        app_controller.going_to_deep_sleep();
        CommonActions::render_sleep_screen();
        ESP::delay(1000);
        inkplate_platform.deep_sleep((gpio_num_t)0, 0);
      }
    }
  }
#else
  while (true) { }
#endif
}

const EventMgr::Event & EventMgr::get_event()
{
  static Event event{ EventKind::NONE, 0, 0, 0 };

#if EPUB_INKPLATE_BUILD
  if (input_event_queue == nullptr) {
    event.kind = EventKind::NONE;
    vTaskDelay(pdMS_TO_TICKS(1000));
    return event;
  }

  if (!xQueueReceive(input_event_queue, &event, pdMS_TO_TICKS(1000))) {
    event.kind = EventKind::NONE;
  }
#endif

  return event;
}

void EventMgr::set_orientation(Screen::Orientation)
{
}

#endif // BOARD_TYPE_PAPER_S3
