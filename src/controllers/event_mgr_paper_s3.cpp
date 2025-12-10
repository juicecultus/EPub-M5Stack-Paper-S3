// Paper S3 EventMgr stub implementation
// Provides a minimal, no-input EventMgr so the EPUB app can run
// on BOARD_TYPE_PAPER_S3 without Inkplate-specific key handling.

#include "global.hpp"

#if defined(BOARD_TYPE_PAPER_S3)

#include "controllers/event_mgr.hpp"
#include "controllers/app_controller.hpp"
#include "screen.hpp"

#if EPUB_INKPLATE_BUILD
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "freertos/queue.h"
  #include "driver/i2c.h"
  #include "esp_log.h"
#endif

EventMgr event_mgr;

#if EPUB_INKPLATE_BUILD

static constexpr char const * TAG = "EventMgrPaperS3";

static const gpio_num_t PAPERS3_GT911_SDA_GPIO = GPIO_NUM_41;
static const gpio_num_t PAPERS3_GT911_SCL_GPIO = GPIO_NUM_42;
static const i2c_port_t PAPERS3_GT911_I2C_PORT = I2C_NUM_0;

static uint8_t gt911_addr = 0x14;
static bool    gt911_ok   = false;

static QueueHandle_t input_event_queue = nullptr;

static esp_err_t gt911_write_reg(uint8_t addr, uint16_t reg, const uint8_t * data, size_t len)
{
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  if (cmd == nullptr) return ESP_FAIL;

  uint8_t reg_hi = reg >> 8;
  uint8_t reg_lo = reg & 0xFF;

  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg_hi, true);
  i2c_master_write_byte(cmd, reg_lo, true);
  if ((data != nullptr) && (len != 0)) {
    i2c_master_write(cmd, (uint8_t *)data, len, true);
  }
  i2c_master_stop(cmd);

  esp_err_t ret = i2c_master_cmd_begin(PAPERS3_GT911_I2C_PORT, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);
  return ret;
}

static esp_err_t gt911_read_reg(uint8_t addr, uint16_t reg, uint8_t * data, size_t len)
{
  if ((data == nullptr) || (len == 0)) return ESP_ERR_INVALID_ARG;

  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  if (cmd == nullptr) return ESP_FAIL;

  uint8_t reg_hi = reg >> 8;
  uint8_t reg_lo = reg & 0xFF;

  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg_hi, true);
  i2c_master_write_byte(cmd, reg_lo, true);

  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);

  if (len > 1) {
    i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
  }
  i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
  i2c_master_stop(cmd);

  esp_err_t ret = i2c_master_cmd_begin(PAPERS3_GT911_I2C_PORT, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);
  return ret;
}

static bool gt911_read_point(uint16_t * x, uint16_t * y)
{
  if (!gt911_ok || (x == nullptr) || (y == nullptr)) return false;

  uint8_t status = 0;
  if (gt911_read_reg(gt911_addr, 0x814E, &status, 1) != ESP_OK) return false;

  if ((status & 0x80) == 0) return false;

  uint8_t points = status & 0x0F;
  if (points == 0) {
    uint8_t zero = 0;
    gt911_write_reg(gt911_addr, 0x814E, &zero, 1);
    return false;
  }

  uint8_t data[4] = { 0 };
  if (gt911_read_reg(gt911_addr, 0x8150, data, sizeof(data)) != ESP_OK) return false;

  *x = (uint16_t)((data[1] << 8) | data[0]);
  *y = (uint16_t)((data[3] << 8) | data[2]);

  uint8_t zero = 0;
  gt911_write_reg(gt911_addr, 0x814E, &zero, 1);

  return true;
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

  bool       touch_active = false;
  bool       hold_sent    = false;
  uint16_t   start_x      = 0;
  uint16_t   start_y      = 0;
  uint16_t   current_x    = 0;
  uint16_t   current_y    = 0;
  TickType_t start_tick   = 0;

  while (true) {
    uint16_t x = 0;
    uint16_t y = 0;

    bool has_touch = gt911_read_point(&x, &y);

    if (has_touch) {
      if (!touch_active) {
        // First contact
        touch_active = true;
        hold_sent    = false;
        start_tick   = xTaskGetTickCount();
        start_x      = current_x = x;
        start_y      = current_y = y;
      }
      else {
        // Update current finger position while it moves.
        current_x = x;
        current_y = y;
      }

      // Detect a long press while the finger is still down.
      if (touch_active && !hold_sent) {
        TickType_t now   = xTaskGetTickCount();
        uint32_t   dt_ms = (now - start_tick) * portTICK_PERIOD_MS;

        int dx = (int)current_x - (int)start_x;
        int dy = (int)current_y - (int)start_y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;

        if ((dt_ms >= longpress_ms) &&
            (dx <= (int)longpress_move_threshold) &&
            (dy <= (int)longpress_move_threshold)) {

          EventMgr::Event ev;
          ev.kind = EventMgr::EventKind::HOLD;
          ev.x    = start_x;
          ev.y    = start_y;
          ev.dist = 0;

          if (input_event_queue != nullptr) {
            xQueueSend(input_event_queue, &ev, 0);
          }

          hold_sent = true;
        }
      }
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

        if (hold_sent) {
          // End of a long-press sequence.
          ev.kind = EventMgr::EventKind::RELEASE;
        }
        else if ((abs_dx > abs_dy) && (abs_dx > (int)swipe_threshold)) {
          // Horizontal swipe for page-level navigation.
          ev.kind = (dx > 0) ? EventMgr::EventKind::SWIPE_RIGHT
                             : EventMgr::EventKind::SWIPE_LEFT;
        }
        else {
          // Short interaction: treat as a TAP.
          ev.kind = EventMgr::EventKind::TAP;
        }

        if ((ev.kind != EventMgr::EventKind::NONE) && (input_event_queue != nullptr)) {
          xQueueSend(input_event_queue, &ev, 0);
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

#endif // EPUB_INKPLATE_BUILD

bool EventMgr::setup()
{
#if EPUB_INKPLATE_BUILD
  if (input_event_queue == nullptr) {
    input_event_queue = xQueueCreate(10, sizeof(Event));
  }

  i2c_config_t conf = {};
  conf.mode = I2C_MODE_MASTER;
  conf.sda_io_num = PAPERS3_GT911_SDA_GPIO;
  conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
  conf.scl_io_num = PAPERS3_GT911_SCL_GPIO;
  conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
  conf.master.clk_speed = 400000;

  esp_err_t err = i2c_param_config(PAPERS3_GT911_I2C_PORT, &conf);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2c_param_config failed: %d", (int)err);
  }
  else {
    err = i2c_driver_install(PAPERS3_GT911_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
      ESP_LOGE(TAG, "i2c_driver_install failed: %d", (int)err);
    }
    else {
      uint8_t buf = 0;
      if (gt911_read_reg(0x14, 0x8140, &buf, 1) == ESP_OK) {
        gt911_addr = 0x14;
        gt911_ok   = true;
        ESP_LOGI(TAG, "GT911 detected at 0x14");
      }
      else if (gt911_read_reg(0x5D, 0x8140, &buf, 1) == ESP_OK) {
        gt911_addr = 0x5D;
        gt911_ok   = true;
        ESP_LOGI(TAG, "GT911 detected at 0x5D");
      }
      else {
        ESP_LOGE(TAG, "GT911 not found on I2C bus");
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
  while (true) {
    const Event & event = get_event();

    if (event.kind != EventKind::NONE) {
      app_controller.input_event(event);
      return;
    }
  }
#else
  while (true) { }
#endif
}

const EventMgr::Event & EventMgr::get_event()
{
  static Event event{ EventKind::NONE };

#if EPUB_INKPLATE_BUILD
  if (input_event_queue == nullptr) {
    event.kind = EventKind::NONE;
    vTaskDelay(pdMS_TO_TICKS(1000));
    return event;
  }

  if (!xQueueReceive(input_event_queue, &event, portMAX_DELAY)) {
    event.kind = EventKind::NONE;
  }
#endif

  return event;
}

void EventMgr::set_orientation(Screen::Orientation)
{
}

#endif // BOARD_TYPE_PAPER_S3
