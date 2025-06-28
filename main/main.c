/**
 ******************************************************************************
 * @file           : main.c
 * @author         : Mauricio Barroso Benavides
 * @date           : Apr 16, 2023
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * MIT License
 *
 * Copyright (c) 2023 Mauricio Barroso Benavides
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_system.h"
#include "esp_wifi_types_generic.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"

#include "driver/i2c_master.h"
#include "lwip/ip4_addr.h"
#include "lwip/lwip_napt.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "netdb.h"
#include "portmacro.h"
#include "sdkconfig.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"

#include "at24cs0x.h"
#include "button.h"
#include "buzzer.h"
#include "fsm.h"
#include "led.h"
#include "tpl5010.h"

#include "clients.c"
#include "misc.c"
#include "nvs.c"
#include "server.c"
#include "settings.c"
#include "typedefs.h"

/* Macros --------------------------------------------------------------------*/
/* Peripherals GPIOs macros */
#define I2C_BUS_SDA_PIN CONFIG_PERIPHERALS_I2C_SDA_PIN
#define I2C_BUS_SCL_PIN CONFIG_PERIPHERALS_I2C_SCL_PIN
#define TPL5010_WAKE_PIN CONFIG_PERIPHERALS_EWDT_WAKE_PIN
#define TPL5010_DONE_PIN CONFIG_PERIPHERALS_EWDT_DONE_PIN
#define BUTTON_PIN CONFIG_PERIPHERALS_BUTTON_PIN
#define BUZZER_PIN CONFIG_PERIPHERALS_BUZZER_PIN
#define LED_PIN CONFIG_PERIPHERALS_LEDS_PIN

/* SPIFFS macros */
#define SPIFFS_BASE_PATH "/spiffs"

/**/
#define APP_QUEUE_LEN_DEFAULT 5

/**/
#define APP_ROUTE_CMD_MAX 3

/**/
#define APP_TASK_HEALTH_MONITOR_PRIORITY tskIDLE_PRIORITY + 1
#define APP_TASK_ACTIONS_PRIORITY tskIDLE_PRIORITY + 2
#define APP_TASK_ALERTS_PRIORITY tskIDLE_PRIORITY + 3
#define APP_TASK_NETWORK_PRIORITY tskIDLE_PRIORITY + 4
#define APP_TASK_CLIENTS_PRIORITY tskIDLE_PRIORITY + 5
#define APP_TASK_TICK_PRIORITY tskIDLE_PRIORITY + 6
#define APP_TASK_RESPONSES_MANAGER_PRIORITY tskIDLE_PRIORITY + 8
#define APP_TASK_TRIGGERS_MANAGER_PRIORITY tskIDLE_PRIORITY + 9

/* Typedef -------------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
/* Tag for debug */
static const char *TAG = "NearFi";

static uint8_t serial_number[AT24CS0X_SN_SIZE];
static uint8_t mac_addr[6];
static settings_t settings;
static clients_t clients;
static uint32_t otp = 0;

/* Components */
static button_t button;
static led_t led;
static buzzer_t buzzer;
static at24cs0x_t eeprom;
static tpl5010_t wdt;
static i2c_master_bus_handle_t i2c_bus_handle;
static fsm_t fsm;

/* OTA variables */
#ifdef CONFIG_OTA_ENABLE
extern const uint8_t ota_cert[] asm("_binary_server_pem_start");
static char *ota_url = CONFIG_OTA_FILE_URL;
#endif /* CONFIG_OTA_ENABLE */

/* Buzzer sounds */
sound_t sound_beep[] = {
    {880, 100, 100}, // La5
};

sound_t sound_warning[] = {
    {784, 150, 100},
    {659, 150, 100},
    {784, 150, 100},
};
sound_t sound_success[] = {
    {784, 120, 100},
    {988, 180, 100},
    {1175, 220, 80},
};

sound_t sound_fail[] = {
    {880, 200, 100},
    {698, 180, 100},
    {523, 250, 100},
};

sound_t sound_startup[] = {
    {1000, 80, 90}, {1500, 100, 100}, {2000, 120, 100},
    {1500, 60, 80}, {1800, 100, 90},
};

/* Input and Output queues */
static QueueHandle_t event_triggers_queue;
static QueueHandle_t event_responses_queue;
static QueueHandle_t clients_commands_queue;
static QueueHandle_t actions_commands_queue;
static QueueHandle_t network_commands_queue;
static QueueHandle_t alerts_commands_queue;

/* Commands queue array */
static QueueHandle_t event_cmd_queues[EVENT_CMD_MAX] = {0};

/* Triggers to commands map  */
static event_cmd_t event_trg_map[EVENT_TRG_MAX][APP_ROUTE_CMD_MAX];

/* Responses to commands map  */
static event_cmd_t event_rsp_map[EVENT_RSP_MAX][APP_ROUTE_CMD_MAX];

/* Alerts FSM events */
static int alerts_process = ALERTS_PROCESS_CLEAR;
static int alerts_idle = ALERTS_IDLE_CLEAR;
static int alerts_signal = ALERTS_SIGNAL_CLEAR;
static bool is_full = false;

/* Alerts LEDs color variables */
static led_rgb_t idle_rgb;
static led_rgb_t process_rgb;

/* Private function prototypes -----------------------------------------------*/
/* Initialization functions */
static esp_err_t wifi_init(void);

/* Event handlers */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);
static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data);
static void prov_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);

/* Provisioning utils */
static char *get_device_service_name(const char *ssid_prefix);
static esp_err_t custom_prov_data_handler(uint32_t session_id,
                                          const uint8_t *inbuf, ssize_t inlen,
                                          uint8_t **outbuf, ssize_t *outlen,
                                          void *priv_data);

/* Utils */
static void print_dev_info(void);

/* RTOS tasks */
static void tick_task(void *arg);
static void health_monitor_task(void *arg);
static int tls_health_check(void);

static char *read_http_response(httpd_req_t *req);
static esp_err_t settings_save_handler(httpd_req_t *req);
static esp_err_t settings_load_handler(httpd_req_t *req);
static esp_err_t login_handler(httpd_req_t *req);

static esp_err_t spiffs_init(const char *base_path);

static void delay_ms(uint32_t ms);

/**/
static esp_err_t app_create_queues(void);
static esp_err_t app_create_tasks(void);
static void triggers_manager_task(void *arg);
static void responses_manager_task(void *arg);
static void alerts_task(void *arg);
static void network_task(void *arg);
static void actions_task(void *arg);
static void clients_task(void *arg);

static void button_cb(void *arg);
static void wdt_cb(void *arg);
static int eeprom_read_cb(uint8_t data_addr, uint8_t *data, uint32_t data_len);
static int eeprom_write_cb(uint8_t data_addr, uint8_t *data, uint32_t data_len);

static void event_assign_cmds_queue(int first, int last,
                                    QueueHandle_t cmd_queue);
static void event_register_route(event_cmd_t (*map)[APP_ROUTE_CMD_MAX],
                                 int event, int cmd1, int cmd2, int cmd3);

static void event_send_response(event_t *const me, event_rsp_t rsp);
static void event_send_trigger(event_t *const event, event_rsp_t trg,
                               bool is_isr);
static void event_route(event_t *const event,
                        event_cmd_t (*map)[APP_ROUTE_CMD_MAX]);

static void on_idle_update(void);
static void on_process_enter(void);
static void on_signal_enter(void);

/* Main ----------------------------------------------------------------------*/
void app_main(void) {
  /* Create queues and tasks to manage app events */
  ESP_ERROR_CHECK(app_create_queues());
  ESP_ERROR_CHECK(app_create_tasks());

  /* Assign commands to a process queue according their function */
  event_assign_cmds_queue(EVENT_CMD_ALERTS_IDLE_ONLINE, EVENT_CMD_ALERTS_MAX,
                          alerts_commands_queue);
  event_assign_cmds_queue(EVENT_CMD_NETWORK_OTA, EVENT_CMD_NETWORK_MAX,
                          network_commands_queue);
  event_assign_cmds_queue(EVENT_CMD_CLIENTS_ADD, EVENT_CMD_CLIENTS_MAX,
                          clients_commands_queue);
  event_assign_cmds_queue(EVENT_CMD_ACTIONS_RESET, EVENT_CMD_ACTIONS_MAX,
                          actions_commands_queue);

  /* Register triggers to commands routes */
  event_register_route(event_trg_map, EVENT_TRG_BUTTON_SHORT,
                       EVENT_CMD_ACTIONS_RESET, EVENT_CMD_NO, EVENT_CMD_NO);
  event_register_route(event_trg_map, EVENT_TRG_BUTTON_MEDIUM,
                       EVENT_CMD_NETWORK_OTA, EVENT_CMD_NO, EVENT_CMD_NO);
  event_register_route(event_trg_map, EVENT_TRG_BUTTON_LONG,
                       EVENT_CMD_ACTIONS_RESTORE, EVENT_CMD_NO, EVENT_CMD_NO);

  event_register_route(event_trg_map, EVENT_TRG_WIFI_AP_STACONNECTED,
                       EVENT_CMD_CLIENTS_ADD, EVENT_CMD_NO, EVENT_CMD_NO);
  event_register_route(event_trg_map, EVENT_TRG_WIFI_AP_STADISCONNECTED,
                       EVENT_CMD_CLIENTS_REMOVE, EVENT_CMD_NO, EVENT_CMD_NO);
  event_register_route(event_trg_map, EVENT_TRG_WIFI_STA_DISCONNECTED,
                       EVENT_CMD_NETWORK_RECONNECT,
                       EVENT_CMD_ALERTS_IDLE_DISCONNECTED, EVENT_CMD_NO);

  event_register_route(event_trg_map, EVENT_TRG_PROV_START,
                       EVENT_CMD_ALERTS_PROCESS_PROV, EVENT_CMD_NO,
                       EVENT_CMD_NO);
  event_register_route(event_trg_map, EVENT_TRG_PROV_END,
                       EVENT_CMD_ACTIONS_RESET, EVENT_CMD_NO, EVENT_CMD_NO);
  event_register_route(event_trg_map, EVENT_TRG_PROV_FAIL,
                       EVENT_CMD_ACTIONS_RESTORE, EVENT_CMD_NO, EVENT_CMD_NO);

  event_register_route(event_trg_map, EVENT_TRG_HEALTH_INTERNET,
                       EVENT_CMD_ALERTS_IDLE_ONLINE, EVENT_CMD_NO,
                       EVENT_CMD_NO);
  event_register_route(event_trg_map, EVENT_TRG_HEALTH_NO_INTERNET,
                       EVENT_CMD_ALERTS_IDLE_OFFLINE, EVENT_CMD_NO,
                       EVENT_CMD_NO);

  event_register_route(event_trg_map, EVENT_TRG_WDT, EVENT_CMD_ACTIONS_WDT,
                       EVENT_CMD_NO, EVENT_CMD_NO);
  event_register_route(event_trg_map, EVENT_TRG_TICK, EVENT_CMD_CLIENTS_TICK,
                       EVENT_CMD_NO, EVENT_CMD_NO);
  event_register_route(event_trg_map, EVENT_TRG_IP_GOT,
                       EVENT_CMD_ALERTS_IDLE_ONLINE, EVENT_CMD_NO,
                       EVENT_CMD_NO);

  /* Register responses to commands routes */
  event_register_route(event_rsp_map, EVENT_RSP_ACTIONS_RESTORE_SUCCESS,
                       EVENT_CMD_ACTIONS_RESET, EVENT_CMD_NO, EVENT_CMD_NO);
  event_register_route(event_rsp_map, EVENT_RSP_ACTIONS_RESTORE_FAIL,
                       EVENT_CMD_ALERTS_SIGNAL_FAIL, EVENT_CMD_NO,
                       EVENT_CMD_NO);

  event_register_route(event_rsp_map, EVENT_RSP_NETWORK_OTA_START,
                       EVENT_CMD_ALERTS_PROCESS_OTA, EVENT_CMD_NO,
                       EVENT_CMD_NO);
  event_register_route(event_rsp_map, EVENT_RSP_NETWORK_OTA_SUCCESS,
                       EVENT_CMD_ALERTS_PROCESS_END,
                       EVENT_CMD_ALERTS_SIGNAL_SUCCESS,
                       EVENT_CMD_ACTIONS_RESET);
  event_register_route(event_rsp_map, EVENT_RSP_NETWORK_OTA_FAIL,
                       EVENT_CMD_ALERTS_PROCESS_END,
                       EVENT_CMD_ALERTS_SIGNAL_FAIL, EVENT_CMD_NO);
  event_register_route(event_rsp_map, EVENT_RSP_NETWORK_OTA_TIMEOUT,
                       EVENT_CMD_ALERTS_PROCESS_END,
                       EVENT_CMD_ALERTS_SIGNAL_WARNING, EVENT_CMD_NO);
  event_register_route(event_rsp_map, EVENT_RSP_NETWORK_RECONNECT_TIMEOUT,
                       EVENT_CMD_ALERTS_SIGNAL_WARNING, EVENT_CMD_ACTIONS_RESET,
                       EVENT_CMD_NO);

  event_register_route(event_rsp_map, EVENT_RSP_CLIENTS_ADD_SUCCESS,
                       EVENT_CMD_ALERTS_SIGNAL_SUCCESS, EVENT_CMD_NO,
                       EVENT_CMD_NO);
  event_register_route(event_rsp_map, EVENT_RSP_CLIENTS_ADD_FAIL,
                       EVENT_CMD_NETWORK_DEAUTH, EVENT_CMD_NO, EVENT_CMD_NO);
  event_register_route(event_rsp_map, EVENT_RSP_CLIENTS_ADD_FULL,
                       EVENT_CMD_ALERTS_IDLE_FULL, EVENT_CMD_NO, EVENT_CMD_NO);
  event_register_route(event_rsp_map, EVENT_RSP_CLIENTS_REMOVE_EMPTY,
                       EVENT_CMD_ALERTS_IDLE_NO_FULL, EVENT_CMD_NO,
                       EVENT_CMD_NO);
  event_register_route(event_rsp_map, EVENT_RSP_CLIENTS_REMOVE_AVAILABLE,
                       EVENT_CMD_ALERTS_IDLE_NO_FULL, EVENT_CMD_NO,
                       EVENT_CMD_NO);
  event_register_route(event_rsp_map, EVENT_RSP_CLIENTS_TICK_TIMEOUT,
                       EVENT_CMD_NETWORK_DEAUTH, EVENT_CMD_NO, EVENT_CMD_NO);

  /* Initialize a LED instance */
  ESP_ERROR_CHECK(led_strip_init(&led, LED_PIN, 2));

  led_rgb_set_continuous(&led, 128, 0, 150);

  /* Initialize a buzzer instance */
  buzzer_init(&buzzer, BUZZER_PIN, LEDC_TIMER_0, LEDC_CHANNEL_0);

  buzzer_run(&buzzer, sound_startup, 5);

  /* Initialize a button instance */
  ESP_ERROR_CHECK(button_init(&button, BUTTON_PIN, BUTTON_EDGE_FALLING,
                              tskIDLE_PRIORITY + 4,
                              configMINIMAL_STACK_SIZE * 2));

  /* Add butttons callbacks functions for single, medium and log click */
  button_add_cb(&button, BUTTON_CLICK_SINGLE, button_cb,
                (void *)EVENT_TRG_BUTTON_SHORT);
  button_add_cb(&button, BUTTON_CLICK_MEDIUM, button_cb,
                (void *)EVENT_TRG_BUTTON_MEDIUM);
  button_add_cb(&button, BUTTON_CLICK_LONG, button_cb,
                (void *)EVENT_TRG_BUTTON_LONG);

  /* Initialize TPL5010 instance */
  ESP_ERROR_CHECK(
      tpl5010_init(&wdt, TPL5010_WAKE_PIN, TPL5010_DONE_PIN, delay_ms));

  tpl5010_register_callback(&wdt, wdt_cb, NULL);

  /* Initialize I2C bus */
  i2c_master_bus_config_t i2c_bus_config = {.clk_source = I2C_CLK_SRC_DEFAULT,
                                            .i2c_port = I2C_NUM_0,
                                            .scl_io_num = I2C_BUS_SCL_PIN,
                                            .sda_io_num = I2C_BUS_SDA_PIN,
                                            .glitch_ignore_cnt = 7};

  ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle));

  /* Initialize AT24CS02 */
  ESP_ERROR_CHECK(
      at24cs0x_init(&eeprom,              /* AT24CS0X instance */
                    i2c_bus_handle,       /* I2C bus instance */
                    AT24CS0X_I2C_ADDRESS, /* I2C device address */
                    AT24CS0X_MODEL_02));  /* I2C custom write function */

  /* Initializ and load settings */
  settings_init(&settings, eeprom_read_cb, eeprom_write_cb);
  settings_load(&settings);

  /* Initialize NVS */
  ESP_ERROR_CHECK(nvs_init());

  /* Initialize Wi-Fi */
  ESP_ERROR_CHECK(wifi_init());

  /* Initialize clients list */
  clients_init(&clients);

  /* Check if are Wi-Fi credentials provisioned */
  bool provisioned = false;
  ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

  if (provisioned) {
    ESP_LOGI(TAG, "Already provisioned. Connecting to AP...");

    /* Initialize and configure file system and HTTP server */
    ESP_ERROR_CHECK(spiffs_init(SPIFFS_BASE_PATH));
    server_init(SPIFFS_BASE_PATH);
    server_uri_handler_add("/login", HTTP_POST, login_handler);
    server_uri_handler_add("/set_settings", HTTP_POST, settings_save_handler);
    server_uri_handler_add("/get_settings", HTTP_POST, settings_load_handler);

    /* Initialize NAT */
    ip_napt_enable(ipaddr_addr("192.168.4.1"), 1);
    ESP_LOGI(TAG, "NAT is enabled");

    /* Connect to router */
    esp_wifi_connect();
  } else {
    ESP_LOGI(TAG, "Not provisioned. Waiting while the process running...");

    /* Initialize provisioning */
    wifi_prov_mgr_config_t prov_config = {.scheme = wifi_prov_scheme_softap,
                                          .scheme_event_handler =
                                              WIFI_PROV_EVENT_HANDLER_NONE};

    ESP_ERROR_CHECK(wifi_prov_mgr_init(prov_config));

    /* Create endpoint */
    wifi_prov_mgr_endpoint_create("custom-data");

    /* Get SoftAP SSID name */
    char *ap_prov_name = get_device_service_name(CONFIG_WIFI_PROV_SSID_PREFIX);
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, NULL,
                                                     ap_prov_name, NULL));
    free(ap_prov_name);

    /* Register previous created endpoint */
    wifi_prov_mgr_endpoint_register("custom-data", custom_prov_data_handler,
                                    NULL);
  }
}

/* Private function definition -----------------------------------------------*/
/* Initialization functions */
static esp_err_t wifi_init(void) {
  esp_err_t ret;

  ESP_LOGI(TAG, "Initializing Wi-Fi...");

  /* Initialize stack TCP/IP */
  ret = esp_netif_init();

  if (ret != ESP_OK) {
    return ret;
  }

  /* Create event loop */
  ret = esp_event_loop_create_default();

  if (ret != ESP_OK) {
    return ret;
  }

  /* Create netif instances */
  esp_netif_create_default_wifi_sta();
  esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();

  /* Set DHCP server */
  ret = esp_netif_dhcps_stop(ap_netif);
  if (ret != ESP_OK) {
    return ret;
  }

  uint32_t dhcps_lease_time = 2 * 15;
  ret = esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET,
                               ESP_NETIF_IP_ADDRESS_LEASE_TIME,
                               &dhcps_lease_time, sizeof(dhcps_lease_time));

  if (ret != ESP_OK) {
    return ret;
  }

  esp_netif_dns_info_t dns_info = {0};
  dns_info.ip.u_addr.ip4.addr = ipaddr_addr("8.8.8.8");
  dns_info.ip.type = IPADDR_TYPE_V4;
  ret = esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns_info);
  if (ret != ESP_OK) {
    return ret;
  }

  uint8_t dns_offer = 1;
  ret = esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET,
                               ESP_NETIF_DOMAIN_NAME_SERVER, &dns_offer,
                               sizeof(dns_offer));

  if (ret != ESP_OK) {
    return ret;
  }

  ret = esp_netif_dhcps_start(ap_netif);
  if (ret != ESP_OK) {
    return ret;
  }

  /* Initialize Wi-Fi driver */
  wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&wifi_init_config);

  /* Declare event handler instances for Wi-Fi and IP */
  esp_event_handler_instance_t instance_any_wifi;
  esp_event_handler_instance_t instance_got_ip;
  esp_event_handler_instance_t instance_any_prov;

  /* Register Wi-Fi, IP and SmartConfig event handlers */
  ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            &wifi_event_handler, NULL,
                                            &instance_any_wifi);

  if (ret != ESP_OK) {
    return ret;
  }

  ret = esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL, &instance_got_ip);

  if (ret != ESP_OK) {
    return ret;
  }

  ret = esp_event_handler_instance_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID,
                                            &prov_event_handler, NULL,
                                            &instance_any_prov);

  if (ret != ESP_OK) {
    return ret;
  }

  /* Get MAC address */
  esp_wifi_get_mac(WIFI_IF_STA, mac_addr);

  /* Fill AP Wi-Fi config */
  wifi_config_t wifi_config;
  wifi_config.ap.channel = CONFIG_WIFI_AP_CHANNEL;
  wifi_config.ap.authmode = WIFI_AUTH_OPEN;
  wifi_config.ap.max_connection = settings_get_clients(&settings);

  if (!strcmp(settings_get_ssid(&settings), SETTINGS_SSID_DEFAULT)) {
    char *ssid = get_device_service_name(CONFIG_WIFI_AP_SSID_PREFIX);
    settings_set_ssid(&settings, ssid);
    settings_save(&settings);
    free(ssid);
  }

  strcpy((char *)wifi_config.ap.ssid, settings_get_ssid(&settings));
  wifi_config.ap.ssid_len = strlen(settings.data.ssid);

  /* Set Wi-Fi mode and config */
  ret = esp_wifi_set_mode(WIFI_MODE_APSTA);

  if (ret != ESP_OK) {
    return ret;
  }

  ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);

  if (ret != ESP_OK) {
    return ret;
  }

  /* Start Wi-Fi */
  ret = esp_wifi_start();

  if (ret != ESP_OK) {
    return ret;
  }

  /* Return ESP_OK */
  return ret;
}

/* Event handlers */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  event_t event;

  switch (event_id) {
  case WIFI_EVENT_STA_DISCONNECTED: {
    ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED");
    event_send_trigger(&event, EVENT_TRG_WIFI_STA_DISCONNECTED, false);
    break;
  }

  case WIFI_EVENT_AP_STACONNECTED: {
    ESP_LOGI(TAG, "WIFI_EVENT_AP_STACONNECTED");
    event.data.client.aid = ((wifi_event_ap_staconnected_t *)event_data)->aid;

    for (uint8_t i = 0; i < 6; i++) {
      event.data.client.mac[i] =
          ((wifi_event_ap_staconnected_t *)event_data)->mac[i];
    }

    event_send_trigger(&event, EVENT_TRG_WIFI_AP_STACONNECTED, false);
    break;
  }

  case WIFI_EVENT_AP_STADISCONNECTED: {
    ESP_LOGI(TAG, "WIFI_EVENT_AP_STADISCONNECTED");

    event.data.client.aid = ((wifi_event_ap_staconnected_t *)event_data)->aid;

    for (uint8_t i = 0; i < 6; i++) {
      event.data.client.mac[i] =
          ((wifi_event_ap_staconnected_t *)event_data)->mac[i];
    }

    event_send_trigger(&event, EVENT_TRG_WIFI_AP_STADISCONNECTED, false);
    break;
  }

  default:
    ESP_LOGI(TAG, "Other Wi-Fi event");
    break;
  }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data) {
  event_t event;

  switch (event_id) {
  case IP_EVENT_STA_GOT_IP: {
    ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP");
    event_send_trigger(&event, EVENT_TRG_IP_GOT, false);
    break;
  }

  default:
    ESP_LOGI(TAG, "Other IP event");
    break;
  }
}

static void prov_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  event_t event;

  switch (event_id) {
  case WIFI_PROV_START: {
    ESP_LOGI(TAG, "WIFI_PROV_START");
    event_send_trigger(&event, EVENT_TRG_PROV_START, false);
    break;
  }

  case WIFI_PROV_END: {
    ESP_LOGI(TAG, "WIFI_PROV_END");
    event_send_trigger(&event, EVENT_TRG_PROV_END, false);
    break;
  }

  case WIFI_PROV_CRED_FAIL: {
    ESP_LOGI(TAG, "WIFI_PROV_CRED_FAIL");
    event_send_trigger(&event, EVENT_TRG_PROV_FAIL, false);
    break;
  }

  default: {
    ESP_LOGI(TAG, "Other event");
    break;
  }
  }
}

/* Provisioning utils */
static char *get_device_service_name(const char *ssid_prefix) {
  char *name = NULL;

  name = malloc((strlen(ssid_prefix) + 6 + 1) * sizeof(*name));
  sprintf(name, "%s%02X%02X%02X", ssid_prefix, mac_addr[3], mac_addr[4],
          mac_addr[5]);

  return name;
}

static esp_err_t custom_prov_data_handler(uint32_t session_id,
                                          const uint8_t *inbuf, ssize_t inlen,
                                          uint8_t **outbuf, ssize_t *outlen,
                                          void *priv_data) {
  if (inbuf) {
    ESP_LOGI(TAG, "Received data: %.*s", inlen, (char *)inbuf);
  }

  char response[] = "88cb3bdf-2735-425e-8d4c-5e4e23eb8bdc/data_out";
  *outbuf = (uint8_t *)strdup(response);

  if (*outbuf == NULL) {
    ESP_LOGE(TAG, "System out of memory");
    return ESP_ERR_NO_MEM;
  }

  *outlen = strlen(response) + 1; /* +1 for NULL terminating byte */

  return ESP_OK;
}

/* RTOS tasks */
static void tick_task(void *arg) {
  TickType_t last_wake = xTaskGetTickCount();
  event_t event;

  for (;;) {
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
    event_send_trigger(&event, EVENT_TRG_TICK, false);
  }
}

static void health_monitor_task(void *arg) {
  TickType_t last_wake = xTaskGetTickCount();
  event_t event;

  for (;;) {
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10000));

    if (!tls_health_check()) {
      event_send_trigger(&event, EVENT_TRG_HEALTH_INTERNET, false);
    } else {
      event_send_trigger(&event, EVENT_TRG_HEALTH_NO_INTERNET, false);
    }
  }
}

static int tls_health_check(void) {
  const char *host = "google.com";
  const char *port = "443";
  struct addrinfo hints = {
      .ai_family = AF_INET,
      .ai_socktype = SOCK_STREAM,
  };
  struct addrinfo *res = NULL;
  int ret = -1, sock;

  if (getaddrinfo(host, port, &hints, &res) != 0 || res == NULL) {
    goto cleanup;
  }

  sock = socket(res->ai_family, res->ai_socktype, 0);
  if (sock < 0) {
    goto cleanup;
  }

  struct timeval timeout = {.tv_sec = 5, .tv_usec = 0};
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  if (connect(sock, res->ai_addr, res->ai_addrlen) == 0) {
    ret = 0; /* Return OK */
  }

  close(sock);

cleanup:
  if (res) {
    freeaddrinfo(res);
  }
  return ret;
}

/* Utils */
static void print_dev_info(void) {
  char *ap_prov_name = get_device_service_name(CONFIG_WIFI_PROV_SSID_PREFIX);

  at24cs0x_read_serial_number(&eeprom, serial_number);

  ESP_LOGI("info",
           "%s,%02X%02X%02X%02X%02X%02X,%02X%02X%02X%02X%02X%02X%02X%02X%"
           "02X%02X%02X%02X%02X%02X%02X%02X",
           ap_prov_name, mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3],
           mac_addr[4], mac_addr[5], serial_number[0], serial_number[1],
           serial_number[2], serial_number[3], serial_number[4],
           serial_number[5], serial_number[6], serial_number[7],
           serial_number[8], serial_number[9], serial_number[10],
           serial_number[11], serial_number[12], serial_number[13],
           serial_number[14], serial_number[15]);

  free(ap_prov_name);
}

static char *read_http_response(httpd_req_t *req) {
  int remaining = req->content_len;
  char *buf = ((struct file_server_data *)req->user_ctx)->scratch;
  int received;

  while (remaining > 0) {
    ESP_LOGI("server", "Remaining size : %d", remaining);
    /* Receive the file part by part into a buffer */
    if ((received =
             httpd_req_recv(req, buf, MIN(remaining, SCRATCH_BUFSIZE))) <= 0) {
      if (received == HTTPD_SOCK_ERR_TIMEOUT) {
        /* Retry if timeout occurred */
        continue;
      }

      ESP_LOGE("server", "File reception failed!");
      /* Respond with 500 Internal Server Error */
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                          "Failed to receive file");
      return NULL;
    }

    remaining -= received;
  }

  /* Truncate the response string */
  buf[req->content_len] = '\0';

  return buf;
}

static esp_err_t settings_save_handler(httpd_req_t *req) {
  char otp_header[11];

  if (httpd_req_get_hdr_value_str(req, "Otp", otp_header, sizeof(otp_header)) ==
      ESP_OK) {
    if (otp == (uint32_t)strtoul(otp_header, NULL, 10)) {
      /* Get response */
      char *buf = read_http_response(req);

      settings_t new_settings;
      sscanf(buf, "%hhu,%hu,%31s", &new_settings.data.clients_num,
             &new_settings.data.time, new_settings.data.ssid);

      printf("buffer:%d,%d,%s\r\n", settings.data.clients_num,
             settings.data.time, settings.data.ssid);

      /* Write the new data in EEPROM */
      if (strlen(new_settings.data.ssid) > 4) {
        strcpy(settings.data.ssid, new_settings.data.ssid);
      }

      if (new_settings.data.clients_num <= 15) {
        settings.data.clients_num = new_settings.data.clients_num;
      }

      if (new_settings.data.time > 0) {
        settings.data.time = new_settings.data.time;
      }

      if (settings_save(&settings)) {
        /* Process the response */
        const char *resp_str = "success";
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, resp_str, strlen(resp_str));
        reset_device(NULL);
      }

    } else {
      httpd_resp_send_500(req);
    }
  }

  /* Respond with an empty chunk to signal HTTP response completion */
  httpd_resp_set_hdr(req, "Connection", "close");
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t settings_load_handler(httpd_req_t *req) {
  /* Check the OTP received */
  char otp_header[11];

  if (httpd_req_get_hdr_value_str(req, "Otp", otp_header, sizeof(otp_header)) ==
      ESP_OK) {
    if (otp == (uint32_t)strtoul(otp_header, NULL, 10)) {
      char resp_str[128];
      sprintf(resp_str, "%d,%d,%s", settings.data.clients_num,
              settings.data.time, settings.data.ssid);
      httpd_resp_set_type(req, "text/plain");
      httpd_resp_send(req, resp_str, strlen(resp_str));
    } else {
      httpd_resp_send_500(req);
    }
  }

  /* Respond with an empty chunk to signal HTTP response completion */
  httpd_resp_set_hdr(req, "Connection", "close");
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t login_handler(httpd_req_t *req) {
  /* Get response */
  char *password = read_http_response(req);

  if (password) {
    /* Check if the password is correct */
    char password_auth[7];
    sprintf(password_auth, "%02X%02X%02X", mac_addr[3], mac_addr[4],
            mac_addr[5]);

    if (!strcmp(password, password_auth)) {
      otp = esp_random();
      char resp_str[128];
      sprintf(resp_str, "%lu", otp);
      httpd_resp_set_type(req, "text/plain");
      httpd_resp_send(req, resp_str, strlen(resp_str));
    } else {
      httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, NULL);
    }

  } else {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
  }

  /* Respond with an empty chunk to signal HTTP response completion */
  httpd_resp_set_hdr(req, "Connection", "close");
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t spiffs_init(const char *base_path) {
  ESP_LOGI("server", "Initializing SPIFFS");

  esp_err_t ret = ESP_OK;

  esp_vfs_spiffs_conf_t conf = {.base_path = base_path,
                                .partition_label = NULL,
                                .max_files = 5,
                                .format_if_mount_failed = false};

  /* Register SPIFFS file system */
  ret = esp_vfs_spiffs_register(&conf);

  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      ESP_LOGE("server", "Failed to mount or format filesystem");
    } else if (ret == ESP_ERR_NOT_FOUND) {
      ESP_LOGE("server", "Failed to find SPIFFS partition");
    } else {
      ESP_LOGE("server", "Failed to initialize SPIFFS (%s)",
               esp_err_to_name(ret));
    }

    return ret;
  }

  size_t total = 0, used = 0;
  ret = esp_spiffs_info(NULL, &total, &used);
  if (ret != ESP_OK) {
    ESP_LOGE("server", "Failed to get SPIFFS partition information (%s)",
             esp_err_to_name(ret));
  } else {
    ESP_LOGI("server", "Partition size: total: %d, used: %d", total, used);
  }

  return ret;
}

static void delay_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

static esp_err_t app_create_queues(void) {
  ESP_LOGI(TAG, "Creating app queues...");

  /* */
  event_triggers_queue = xQueueCreate(APP_QUEUE_LEN_DEFAULT, sizeof(event_t));

  if (event_triggers_queue == NULL) {
    return ESP_FAIL;
  }

  event_responses_queue = xQueueCreate(APP_QUEUE_LEN_DEFAULT, sizeof(event_t));

  if (event_responses_queue == NULL) {
    return ESP_FAIL;
  }

  /**/
  clients_commands_queue = xQueueCreate(APP_QUEUE_LEN_DEFAULT, sizeof(event_t));

  if (clients_commands_queue == NULL) {
    return ESP_FAIL;
  }

  actions_commands_queue = xQueueCreate(APP_QUEUE_LEN_DEFAULT, sizeof(event_t));

  if (actions_commands_queue == NULL) {
    return ESP_FAIL;
  }

  alerts_commands_queue = xQueueCreate(APP_QUEUE_LEN_DEFAULT, sizeof(event_t));

  if (alerts_commands_queue == NULL) {
    return ESP_FAIL;
  }

  network_commands_queue = xQueueCreate(APP_QUEUE_LEN_DEFAULT, sizeof(event_t));

  if (network_commands_queue == NULL) {
    return ESP_FAIL;
  }

  return ESP_OK;

  /**/
}

static esp_err_t app_create_tasks(void) {
  BaseType_t status;

  /**/
  status = xTaskCreatePinnedToCore(tick_task, "Tick Task",
                                   configMINIMAL_STACK_SIZE * 2, NULL,
                                   APP_TASK_TICK_PRIORITY, NULL, 1);

  if (status != pdPASS) {
    return ESP_FAIL;
  }

  status = xTaskCreatePinnedToCore(health_monitor_task, "Health Monitor Task",
                                   configMINIMAL_STACK_SIZE * 2, NULL,
                                   APP_TASK_HEALTH_MONITOR_PRIORITY, NULL, 0);

  if (status != pdPASS) {
    return ESP_FAIL;
  }

  status = xTaskCreatePinnedToCore(triggers_manager_task, "Events Manager Task",
                                   configMINIMAL_STACK_SIZE * 4, NULL,
                                   APP_TASK_TRIGGERS_MANAGER_PRIORITY, NULL, 1);

  if (status != pdPASS) {
    return ESP_FAIL;
  }

  status =
      xTaskCreatePinnedToCore(responses_manager_task, "Responses Manager Task",
                              configMINIMAL_STACK_SIZE * 4, NULL,
                              APP_TASK_RESPONSES_MANAGER_PRIORITY, NULL, 1);

  if (status != pdPASS) {
    return ESP_FAIL;
  }

  status = xTaskCreatePinnedToCore(alerts_task, "Alerts Task",
                                   configMINIMAL_STACK_SIZE * 2, NULL,
                                   APP_TASK_ALERTS_PRIORITY, NULL, 1);

  if (status != pdPASS) {
    return ESP_FAIL;
  }

  status = xTaskCreatePinnedToCore(network_task, "Network Task",
                                   configMINIMAL_STACK_SIZE * 4, NULL,
                                   APP_TASK_NETWORK_PRIORITY, NULL, 0);

  if (status != pdPASS) {
    return ESP_FAIL;
  }

  status = xTaskCreatePinnedToCore(actions_task, "Actions Task",
                                   configMINIMAL_STACK_SIZE * 2, NULL,
                                   APP_TASK_ACTIONS_PRIORITY, NULL, 1);

  if (status != pdPASS) {
    return ESP_FAIL;
  }

  status = xTaskCreatePinnedToCore(clients_task, "Clients Task",
                                   configMINIMAL_STACK_SIZE * 2, NULL,
                                   APP_TASK_CLIENTS_PRIORITY, NULL, 1);

  if (status != pdPASS) {
    return ESP_FAIL;
  }

  return ESP_OK;
}

static void triggers_manager_task(void *arg) {
  BaseType_t status;
  event_t event;

  for (;;) {
    status = xQueueReceive(event_triggers_queue, &event, portMAX_DELAY);

    if (status == pdPASS) {
      event_route(&event, event_trg_map);
    }
  }
}

static void responses_manager_task(void *arg) {
  BaseType_t status;
  event_t event;

  for (;;) {
    status = xQueueReceive(event_responses_queue, &event, portMAX_DELAY);

    if (status == pdPASS) {
      event_route(&event, event_rsp_map);
    }
  }
}

static void alerts_task(void *arg) {
  fsm_init(&fsm, STATE_ALERTS_IDLE);

  fsm_trans_t *trans = NULL;
  fsm_add_transition(&fsm, &trans, STATE_ALERTS_IDLE, STATE_ALERTS_PROCESS,
                     FSM_OP_OR);
  fsm_add_event(&fsm, trans, &alerts_process, ALERTS_PROCESS_PROV);
  fsm_add_event(&fsm, trans, &alerts_process, ALERTS_PROCESS_OTA);

  fsm_add_transition(&fsm, &trans, STATE_ALERTS_IDLE, STATE_ALERTS_SIGNAL,
                     FSM_OP_OR);
  fsm_add_event(&fsm, trans, &alerts_signal, ALERTS_SIGNAL_SUCCESS);
  fsm_add_event(&fsm, trans, &alerts_signal, ALERTS_SIGNAL_FAIL);

  fsm_add_transition(&fsm, &trans, STATE_ALERTS_SIGNAL, STATE_ALERTS_IDLE,
                     FSM_OP_OR);
  fsm_add_event(&fsm, trans, &alerts_process, ALERTS_PROCESS_CLEAR);
  fsm_add_event(&fsm, trans, &alerts_signal, ALERTS_SIGNAL_CLEAR);

  fsm_add_transition(&fsm, &trans, STATE_ALERTS_PROCESS, STATE_ALERTS_SIGNAL,
                     FSM_OP_OR);
  fsm_add_event(&fsm, trans, &alerts_signal, ALERTS_SIGNAL_SUCCESS);
  fsm_add_event(&fsm, trans, &alerts_signal, ALERTS_SIGNAL_FAIL);

  fsm_add_transition(&fsm, &trans, STATE_ALERTS_SIGNAL, STATE_ALERTS_PROCESS,
                     FSM_OP_OR);
  fsm_add_event(&fsm, trans, &alerts_signal, ALERTS_SIGNAL_CLEAR);

  fsm_register_state_actions(&fsm, STATE_ALERTS_IDLE, NULL, on_idle_update,
                             NULL);
  fsm_register_state_actions(&fsm, STATE_ALERTS_PROCESS, on_process_enter, NULL,
                             NULL);
  fsm_register_state_actions(&fsm, STATE_ALERTS_SIGNAL, on_signal_enter, NULL,
                             NULL);

  BaseType_t status;
  event_t event;

  ESP_LOGI(TAG, "Alerts Task created! Waiting for incoming commands");

  for (;;) {
    status = xQueueReceive(alerts_commands_queue, &event, pdMS_TO_TICKS(300));

    if (status == pdPASS) {
      printf("alerts_");
      switch (event.num) {
      case EVENT_CMD_ALERTS_IDLE_ONLINE:
        printf("idle online\r\n");
        alerts_idle = ALERTS_IDLE_ONLINE;
        idle_rgb.r = 0;
        idle_rgb.g = 255;
        idle_rgb.b = 0;
        break;

      case EVENT_CMD_ALERTS_IDLE_OFFLINE:
        printf("idle offline\r\n");
        if (alerts_idle != ALERTS_IDLE_DICONNECTED) {
          alerts_idle = ALERTS_IDLE_OFFLINE;
          idle_rgb.r = 158;
          idle_rgb.g = 98;
          idle_rgb.b = 0;
        }

        break;

      case EVENT_CMD_ALERTS_IDLE_DISCONNECTED:
        printf("idle disconnected\r\n");
        alerts_idle = ALERTS_IDLE_DICONNECTED;
        idle_rgb.r = 255;
        idle_rgb.g = 0;
        idle_rgb.b = 0;
        break;

      case EVENT_CMD_ALERTS_IDLE_FULL:
        printf("idle full\r\n");
        is_full = true;
        break;

      case EVENT_CMD_ALERTS_IDLE_NO_FULL:
        printf("idle no full\r\n");
        is_full = false;
        break;

      case EVENT_CMD_ALERTS_PROCESS_PROV:
        printf("process prov\r\n");
        alerts_process = ALERTS_PROCESS_PROV;
        process_rgb.r = 0;
        process_rgb.g = 0;
        process_rgb.b = 255;
        break;

      case EVENT_CMD_ALERTS_PROCESS_OTA:
        printf("process ota\r\n");
        alerts_process = ALERTS_PROCESS_OTA;
        process_rgb.r = 128;
        process_rgb.g = 128;
        process_rgb.b = 0;
        break;

      case EVENT_CMD_ALERTS_PROCESS_END:
        printf("process end\r\n");
        alerts_process = ALERTS_PROCESS_CLEAR;
        break;

      case EVENT_CMD_ALERTS_SIGNAL_SUCCESS:
        printf("signal success\r\n");
        alerts_signal = ALERTS_SIGNAL_SUCCESS;
        break;

      case EVENT_CMD_ALERTS_SIGNAL_FAIL:
        printf("signal fail\r\n");
        alerts_signal = ALERTS_SIGNAL_FAIL;
        break;

      case EVENT_CMD_ALERTS_SIGNAL_WARNING:
        printf("signal warning\r\n");
        alerts_signal = ALERTS_SIGNAL_WARNING;
        break;

      default:
        break;
      }
    }

    fsm_run(&fsm);
  }
}

static void network_task(void *arg) {
  BaseType_t status;
  event_t event;
  uint8_t reconnect_try = 0;

  ESP_LOGI(TAG, "Network Task created! Waiting for incoming commands");

  for (;;) {
    status = xQueueReceive(network_commands_queue, &event, portMAX_DELAY);
    printf("network_");
    if (status == pdPASS) {
      switch (event.num) {
      case EVENT_CMD_NETWORK_OTA:
        printf("ota\r\n");
        event_send_response(&event, EVENT_RSP_NETWORK_OTA_START);

        if (ota_update(ota_url, (char *)ota_cert, 120000) == ESP_OK) {
          event_send_response(&event, EVENT_RSP_NETWORK_OTA_SUCCESS);
        } else {
          event_send_response(&event, EVENT_RSP_NETWORK_OTA_FAIL);
        }
        break;

      case EVENT_CMD_NETWORK_RECONNECT:
        printf("reconnect\r\n");

        esp_wifi_disconnect();
        esp_wifi_connect();

        if (reconnect_try++ >= 20) {
          event_send_response(&event, EVENT_RSP_NETWORK_RECONNECT_TIMEOUT);
        }
        break;

      case EVENT_CMD_NETWORK_DEAUTH:
        printf("deauth\r\n");
        ESP_LOGE(TAG, MACSTR " DEAUTH", MAC2STR(event.data.client.mac));
        esp_wifi_deauth_sta(event.data.client.aid);
        break;

      default:
        break;
      }
    }
  }
}

static void actions_task(void *arg) {
  BaseType_t status;
  event_t event;

  ESP_LOGI(TAG, "Actions Task created! Waiting for incoming commands");

  for (;;) {
    status = xQueueReceive(actions_commands_queue, &event, portMAX_DELAY);
    printf("actions_");
    if (status == pdPASS) {
      switch (event.num) {
      case EVENT_CMD_ACTIONS_RESET:
        printf("reset\r\n");
        vTaskDelay(pdMS_TO_TICKS((1000)));
        esp_restart();
        break;

      case EVENT_CMD_ACTIONS_RESTORE:
        printf("restore\r\n");

        if (nvs_erase_namespace("nvs.net80211") != ESP_OK) {
          ESP_LOGE(TAG, "Failed to erase Wi-Fi "
                        "credentials");
          event_send_response(&event, EVENT_RSP_ACTIONS_RESTORE_FAIL);
        }
        ESP_LOGI(TAG, "Wi-Fi credentials erased");

        /* Set settings to factory values */
        settings_set_ssid(&settings, SETTINGS_SSID_DEFAULT);
        settings_set_time(&settings, SETTINGS_TIME_DEFAULT);
        settings_set_clients(&settings, SETTINGS_CLIENTS_DEFAULT);
        settings_save(&settings);
        ESP_LOGI(TAG, "Settings set to factory values");

        event_send_response(&event, EVENT_RSP_ACTIONS_RESTORE_SUCCESS);
        break;

      case EVENT_CMD_ACTIONS_WDT:
        printf("wdt\r\n");
        tpl5010_done(&wdt);
        break;

      default:
        printf("other\r\n");
        break;
      }
    }
  }
}

static void clients_task(void *arg) {
  BaseType_t status;
  event_t event;
  wifi_sta_list_t sta_list;

  ESP_LOGI(TAG, "Clients Task created! Waiting for incoming commands");

  for (;;) {
    status = xQueueReceive(clients_commands_queue, &event, portMAX_DELAY);
    //		printf("clients_");
    if (status == pdPASS) {
      switch (event.num) {
      case EVENT_CMD_CLIENTS_ADD:
        printf("add\r\n");

        esp_wifi_ap_get_sta_list(&sta_list);

        for (uint8_t i = 0; i < sta_list.num; i++) {
          if (!strncmp((char *)sta_list.sta[i].mac,
                       (char *)event.data.client.mac, 6)) {
            if (sta_list.sta[i].rssi <= CONFIG_APP_RSSI_THRESHOLD_JOIN) {
              event_send_response(&event, EVENT_RSP_CLIENTS_ADD_FAIL);
            } else {
              clients_add(&clients, event.data.client.mac,
                          event.data.client.aid, settings_get_time(&settings));
              ESP_LOGI(TAG,
                       MACSTR " added to list. "
                              "Clients in list: "
                              "%d/%d",
                       MAC2STR(event.data.client.mac), clients.num,
                       settings_get_clients(&settings));
              event_send_response(&event, EVENT_RSP_CLIENTS_ADD_SUCCESS);

              if (clients.num == settings_get_clients(&settings)) {
                event_send_response(&event, EVENT_RSP_CLIENTS_ADD_FULL);
              }
            }
          }
        }
        break;

      case EVENT_CMD_CLIENTS_REMOVE:
        printf("remove\r\n");

        /* Process */
        clients_remove(&clients, event.data.client.mac);
        ESP_LOGE(TAG,
                 MACSTR " removed from list. Clients "
                        "in list: %d/%d",
                 MAC2STR(event.data.client.mac), clients.num,
                 settings_get_clients(&settings));

        if (clients.num == 0) {
          event_send_response(&event, EVENT_RSP_CLIENTS_REMOVE_EMPTY);
        } else {
          event_send_response(&event, EVENT_RSP_CLIENTS_REMOVE_AVAILABLE);
        }
        break;

      case EVENT_CMD_CLIENTS_TICK:
        //				printf("tick\r\n");
        for (uint8_t i = 0; i < clients.num; i++) {
          if (--clients.client[i].time == 0) {
            event.data.client.aid = clients.client[i].aid;
            strncpy((char *)event.data.client.mac,
                    (char *)clients.client[i].mac, 6);
            event_send_response(&event, EVENT_RSP_CLIENTS_TICK_TIMEOUT);
          }
        }
        break;

      default:
        printf("other\r\n");
        break;
      }
    }
  }
}

static void button_cb(void *arg) {
  event_t event;
  event_send_trigger(&event, (event_trg_t)arg, false);
}

static void wdt_cb(void *arg) {
  event_t event;
  event_send_trigger(&event, EVENT_TRG_WDT, true);
}

static int eeprom_read_cb(uint8_t data_addr, uint8_t *data, uint32_t data_len) {
  return at24cs0x_read(&eeprom, data_addr, data, data_len);
}
static int eeprom_write_cb(uint8_t data_addr, uint8_t *data,
                           uint32_t data_len) {
  return at24cs0x_write(&eeprom, data_addr, data, data_len);
}

static void event_assign_cmds_queue(int first, int last,
                                    QueueHandle_t cmd_queue) {
  for (int cmd = first; cmd < last; cmd++) {
    event_cmd_queues[cmd] = cmd_queue;
  }
}

static void event_register_route(event_cmd_t (*map)[APP_ROUTE_CMD_MAX],
                                 int event, int cmd1, int cmd2, int cmd3) {
  map[event][0] = cmd1;
  map[event][1] = cmd2;
  map[event][2] = cmd3;
}

static void event_send_response(event_t *const event, event_rsp_t rsp) {
  event->num = rsp;
  xQueueSend(event_responses_queue, event, 0);
}

static void event_send_trigger(event_t *const event, event_rsp_t trg,
                               bool is_isr) {
  BaseType_t higher_priority_task_woken = pdFALSE;
  event->num = trg;

  if (is_isr) {
    xQueueSendFromISR(event_triggers_queue, event, &higher_priority_task_woken);
  } else {
    xQueueSend(event_triggers_queue, event, 0);
  }

  portYIELD_FROM_ISR(higher_priority_task_woken);
}

static void event_route(event_t *const event,
                        event_cmd_t (*map)[APP_ROUTE_CMD_MAX]) {
  int num = event->num;
  for (uint8_t i = 0; i < APP_ROUTE_CMD_MAX; i++) {
    int cmd = map[num][i];

    if (cmd > EVENT_CMD_NO && cmd < EVENT_CMD_MAX) {
      event->num = cmd;
      xQueueSend(event_cmd_queues[event->num], event, 0);
    }
  }
}

static void on_idle_update(void) {
  //	printf("\tUPDATE IDLE\t%d\r\n", alerts_idle);
  if (is_full) {
    led_rgb_set_blink(&led, idle_rgb.r, idle_rgb.g, idle_rgb.b, 500, 500);
  } else {
    led_rgb_set_continuous(&led, idle_rgb.r, idle_rgb.g, idle_rgb.b);
  }
}

static void on_process_enter(void) {
  //	printf("\tENTER PROCESS\t%d\r\n", alerts_process);
  led_rgb_set_fade(&led, process_rgb.r, process_rgb.g, process_rgb.b, 1000,
                   1000);
}

static void on_signal_enter(void) {
  //	printf("\tSIGNAL ENTER\t%d\r\n", alerts_signal);
  if (alerts_signal == ALERTS_SIGNAL_SUCCESS) {
    led_rgb_set_continuous(&led, 100, 100, 100);
    buzzer_run(&buzzer, sound_success, 3);
  } else if (alerts_signal == ALERTS_SIGNAL_FAIL) {
    led_rgb_set_continuous(&led, 255, 0, 0);
    buzzer_run(&buzzer, sound_fail, 3);
  } else if (alerts_signal == ALERTS_SIGNAL_WARNING) {
    led_rgb_set_continuous(&led, 128, 128, 0);
    buzzer_run(&buzzer, sound_warning, 3);
  }
  //	vTaskDelay(pdMS_TO_TICKS(300));
  alerts_signal = ALERTS_SIGNAL_CLEAR;
}

/***************************** END OF FILE ************************************/
