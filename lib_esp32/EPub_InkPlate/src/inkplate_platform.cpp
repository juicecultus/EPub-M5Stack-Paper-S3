#include "inkplate_platform.hpp"

#if defined(BOARD_TYPE_PAPER_S3)

#include <cstdio>

#include "logging.hpp"

#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_types.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"

InkPlatePlatform InkPlatePlatform::singleton;
InkPlatePlatform & inkplate_platform = InkPlatePlatform::get_singleton();

// Simple SD card state for Paper S3
static sdmmc_card_t * s_sd_card = nullptr;
static sdmmc_host_t   s_sd_host = SDSPI_HOST_DEFAULT();

bool InkPlatePlatform::setup(bool sd_card_init)
{
  LOG_I("Paper S3 InkPlatePlatform setup (sd_card_init=%d)", sd_card_init ? 1 : 0);

  if (sd_card_init && (s_sd_card == nullptr)) {
    esp_err_t ret;

    // Mount SD card at /sdcard using SPI host and the SD_CARD_PIN_NUM_* pins.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 10,
      .allocation_unit_size = 16 * 1024
    };

    spi_bus_config_t bus_cfg = {
      .mosi_io_num = SD_CARD_PIN_NUM_MOSI,
      .miso_io_num = SD_CARD_PIN_NUM_MISO,
      .sclk_io_num = SD_CARD_PIN_NUM_CLK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 0,
      .flags = 0,
      .intr_flags = 0
    };

    ret = spi_bus_initialize(static_cast<spi_host_device_t>(s_sd_host.slot), &bus_cfg, SPI_DMA_CH_AUTO);
    if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE)) {
      LOG_E("Paper S3: Failed to initialize SD SPI bus (%s)", esp_err_to_name(ret));
      return false;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CARD_PIN_NUM_CS;
    slot_config.host_id = static_cast<spi_host_device_t>(s_sd_host.slot);

    ret = esp_vfs_fat_sdspi_mount("/sdcard", &s_sd_host, &slot_config, &mount_config, &s_sd_card);
    if (ret != ESP_OK) {
      LOG_E("Paper S3: Failed to mount SD card at /sdcard (%s)", esp_err_to_name(ret));
      return false;
    }

    sdmmc_card_print_info(stdout, s_sd_card);
  }

  return true;
}

bool InkPlatePlatform::light_sleep(uint32_t minutes_to_sleep, gpio_num_t gpio_num, int level)
{
  // TODO: Implement proper light sleep with GPIO + timer wake.
  (void)minutes_to_sleep;
  (void)gpio_num;
  (void)level;
  LOG_I("Paper S3 light_sleep stub; not sleeping (minutes=%u)", minutes_to_sleep);
  return false;
}

void InkPlatePlatform::deep_sleep(gpio_num_t gpio_num, int level)
{
  // TODO: Implement proper deep sleep configuration. For now,
  // just log and return so we can keep debugging.
  (void)gpio_num;
  (void)level;
  LOG_I("Paper S3 deep_sleep stub; not sleeping (gpio=%d, level=%d)", (int)gpio_num, level);
}

#endif // BOARD_TYPE_PAPER_S3
