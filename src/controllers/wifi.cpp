#define __WIFI__ 1
#include "controllers/wifi.hpp"

#if EPUB_INKPLATE_BUILD

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"

#include "lwip/err.h"
#include "lwip/sys.h"

static EventGroupHandle_t wifi_event_group       = nullptr;
static bool               wifi_first_start       =    true;
static bool               netif_inited           =   false;
static bool               event_loop_created     =   false;
static bool               wifi_sta_netif_created =   false;
static bool               wifi_driver_inited     =   false;
static bool               wifi_handlers_reg      =   false;

// ----- wifi_sta_event_handler() -----

// The event group allows multiple bits for each event, but we 
// only care about two events:
// - we are connected to the AP with an IP
// - we failed to connect after the maximum amount of retries

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static constexpr int8_t ESP_MAXIMUM_RETRY = 6;

void 
wifi_sta_event_handler(void        * arg, 
                  esp_event_base_t   event_base,
                  int32_t            event_id, 
                  void             * event_data)
{
  LOG_I("WiFi Event Handler: Base: %08x, Event: %d.", (unsigned int) event_base, event_id);

  static int s_retry_num = 0;

  if (event_base == WIFI_EVENT) {
    if (event_id == WIFI_EVENT_STA_START) {
      esp_wifi_connect();
    } 
    else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
      if (wifi_first_start) {
        if (s_retry_num < ESP_MAXIMUM_RETRY) {
          vTaskDelay(pdMS_TO_TICKS(10E3));
          LOG_I("retry to connect to the AP");
          esp_wifi_connect();
          s_retry_num++;
        } 
        else {
          xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
          LOG_I("connect to the AP fail");
        }
      }
      else {
        LOG_I("Wifi Disconnected.");
        vTaskDelay(pdMS_TO_TICKS(10E3));
        LOG_I("retry to connect to the AP");
        esp_wifi_connect();
      }
    } 
  }
  else if (event_base == IP_EVENT) {
    if (event_id == IP_EVENT_STA_GOT_IP) {
      ip_event_got_ip_t * event = (ip_event_got_ip_t*) event_data;
      LOG_I("got ip:" IPSTR, IP2STR(&event->ip_info.ip));
      wifi.set_ip_address(event->ip_info.ip);
      s_retry_num = 0;
      xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
      wifi_first_start = false;
    }
  }
}

bool 
WIFI::start(void)
{
  if (running) return true;

  bool connected = false;
  wifi_first_start = true;

  if (wifi_event_group == nullptr) wifi_event_group = xEventGroupCreate();
  if (wifi_event_group == nullptr) {
    LOG_E("WiFi: failed to create event group (out of memory).");
    return false;
  }

  xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

  // Initialize network interface and default event loop only once for the
  // entire application lifetime. Repeated initialization will return
  // ESP_ERR_INVALID_STATE, so guard these calls explicitly.

  if (!netif_inited) {
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
      LOG_E("esp_netif_init failed (%s).", esp_err_to_name(err));
      stop();
      return false;
    }
    netif_inited = true;
  }

  if (!event_loop_created) {
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK) {
      LOG_E("esp_event_loop_create_default failed (%s).", esp_err_to_name(err));
      stop();
      return false;
    }
    event_loop_created = true;
  }

  if (!wifi_sta_netif_created) {
    if (esp_netif_create_default_wifi_sta() == nullptr) {
      LOG_E("esp_netif_create_default_wifi_sta failed (out of memory)." );
      stop();
      return false;
    }
    wifi_sta_netif_created = true;
  }

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

  #if defined(BOARD_TYPE_PAPER_S3)
    // PaperS3 has tighter internal DRAM constraints; reduce WiFi buffers to
    // improve chances of successful init.
    if (cfg.static_rx_buf_num > 10) cfg.static_rx_buf_num = 10;
    if (cfg.dynamic_rx_buf_num > 16) cfg.dynamic_rx_buf_num = 16;
    if (cfg.cache_tx_buf_num  > 16) cfg.cache_tx_buf_num  = 16;
  #endif

  esp_err_t err = esp_wifi_init(&cfg);
  if (err != ESP_OK) {
    LOG_E("esp_wifi_init failed (%s).", esp_err_to_name(err));
    // Try to cleanup a partially initialized driver; ignore errors.
    (void)esp_wifi_deinit();
    stop();
    return false;
  }
  wifi_driver_inited = true;
  running = true;

  err = esp_event_handler_register(
    WIFI_EVENT, 
    ESP_EVENT_ANY_ID,    
    &wifi_sta_event_handler, 
    NULL);
  if (err != ESP_OK) {
    LOG_E("esp_event_handler_register(WIFI_EVENT) failed (%s).", esp_err_to_name(err));
    stop();
    return false;
  }

  err = esp_event_handler_register(
    IP_EVENT,   
    IP_EVENT_STA_GOT_IP, 
    &wifi_sta_event_handler, 
    NULL);
  if (err != ESP_OK) {
    LOG_E("esp_event_handler_register(IP_EVENT) failed (%s).", esp_err_to_name(err));
    stop();
    return false;
  }
  wifi_handlers_reg = true;

  std::string wifi_ssid;
  std::string wifi_pwd;

  config.get(Config::Ident::SSID, wifi_ssid);
  config.get(Config::Ident::PWD,  wifi_pwd );

  wifi_config_t wifi_config;

  bzero(&wifi_config, sizeof(wifi_config_t));

  wifi_config.sta.bssid_set          = 0;
  wifi_config.sta.pmf_cfg.capable    = true;
  wifi_config.sta.pmf_cfg.required   = false;
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

  strcpy((char *) wifi_config.sta.ssid,     wifi_ssid.c_str());
  strcpy((char *) wifi_config.sta.password, wifi_pwd.c_str());

  err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err != ESP_OK) {
    LOG_E("esp_wifi_set_mode failed (%s).", esp_err_to_name(err));
    stop();
    return false;
  }
  err = esp_wifi_set_config((wifi_interface_t)ESP_IF_WIFI_STA, &wifi_config);
  if (err != ESP_OK) {
    LOG_E("esp_wifi_set_config failed (%s).", esp_err_to_name(err));
    stop();
    return false;
  }
  err = esp_wifi_start();
  if (err != ESP_OK) {
    LOG_E("esp_wifi_start failed (%s).", esp_err_to_name(err));
    stop();
    return false;
  }

  LOG_I("wifi_init_sta finished.");

  // Waiting until either the connection is established (WIFI_CONNECTED_BIT) 
  // or connection failed for the maximum number of re-tries (WIFI_FAIL_BIT). 
  // The bits are set by event_handler() (see above)

  EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
    WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
    pdFALSE,
    pdFALSE,
    pdMS_TO_TICKS(30000));

  if ((bits & (WIFI_CONNECTED_BIT | WIFI_FAIL_BIT)) == 0) {
    LOG_E("WiFi connection timeout.");
  }

  if (bits & WIFI_CONNECTED_BIT) {
    LOG_I("connected to ap SSID:%s password:%s",
            wifi_ssid.c_str(), wifi_pwd.c_str());
    connected = true;
  } 
  else if (bits & WIFI_FAIL_BIT) {
    LOG_E("Failed to connect to SSID:%s, password:%s",
            wifi_ssid.c_str(), wifi_pwd.c_str());
  }
  else {
    LOG_E("UNEXPECTED EVENT");
  }

  return connected;
}

void
WIFI::stop()
{
  // Always attempt to cleanup; never abort on failure.

  if (wifi_handlers_reg) {
    (void)esp_event_handler_unregister(IP_EVENT,   IP_EVENT_STA_GOT_IP, &wifi_sta_event_handler);
    (void)esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,    &wifi_sta_event_handler);
    wifi_handlers_reg = false;
  }

  if (wifi_event_group != nullptr) {
    vEventGroupDelete(wifi_event_group);
    wifi_event_group = nullptr;
  }

  if (wifi_driver_inited) {
    (void)esp_wifi_disconnect();
    (void)esp_wifi_stop();
    (void)esp_wifi_deinit();
    wifi_driver_inited = false;
  }

  running = false;
}

#endif