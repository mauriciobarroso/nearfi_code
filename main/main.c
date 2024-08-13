
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
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_https_ota.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "esp_psram.h"
#include "driver/i2c_master.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"
#include "lwip/ip4_napt.h"
#include "lwip/ip4_addr.h"

#include "led.h"
#include "button.h"
#include "passive_buzzer.c"
#include "at24cs0x.h"
#include "tpl5010.h"
#include "fsm.h"

#include "misc.c"
#include "nvs.c"
#include "cdns.c"
#include "server.c"
#include "typedefs.h"

/* Macros --------------------------------------------------------------------*/
#define BUZZER_SUCCESS()	passive_buzzer_run(&buzzer, sound_success, 3);
#define BUZZER_FAIL()		passive_buzzer_run(&buzzer, sound_warning, 5);
#define BUZZER_ERROR()		passive_buzzer_run(&buzzer, sound_error, 2);
#define BUZZER_BEEP()		passive_buzzer_run(&buzzer, sound_beep, 3);

#define LED_CONNECTED_STATE()		led_rgb_set_continuous(&led, 0, 255, 0)
#define LED_INIT_STATE()			led_rgb_set_continuous(&led, 255, 0, 255)
#define LED_DISCONNECTED_STATE()	led_rgb_set_blink(&led, 255, 0, 0, 500, 500)
#define LED_PROV_STATE()			led_rgb_set_fade(&led, 0, 0, 255, 1000, 1000)
#define LED_OTA_STATE()				led_rgb_set_fade(&led, 255, 255, 0, 1000, 1000)
#define LED_FULL_STATE()			led_rgb_set_continuous(&led, 255, 109, 10)

#define I2C_BUS_SDA_PIN		CONFIG_PERIPHERALS_I2C_SDA_PIN
#define I2C_BUS_SCL_PIN		CONFIG_PERIPHERALS_I2C_SCL_PIN
#define TPL5010_WAKE_PIN	CONFIG_PERIPHERALS_EWDT_WAKE_PIN
#define TPL5010_DONE_PIN	CONFIG_PERIPHERALS_EWDT_DONE_PIN
#define BUTTON_PIN			CONFIG_PERIPHERALS_BUTTON_PIN
#define BUZZER_PIN			CONFIG_PERIPHERALS_BUZZER_PIN
#define LED_PIN				CONFIG_PERIPHERALS_LEDS_PIN

#define SETTINGS_EEPROM_ADDR		0x0
#define SETTINGS_SSID_DEFAULT		"NearFi"
#define SETTINGS_CLIENTS_DEFAULT	15
#define SETTINGS_TIME_DEFAULT		60000

#define SPIFFS_BASE_PATH	"/spiffs"

/* Typedef -------------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
/* Tag for debug */
static const char *TAG = "NearFi";

static TaskHandle_t reconnect_wifi_handle = NULL;
static uint8_t serial_number[AT24CS0X_SN_SIZE];
static uint32_t reconnection_time = CONFIG_APP_RECONNECTION_TIME;
static uint8_t mac_addr[6];
static settings_t settings;
static client_list_t clients;
static uint32_t otp = 0;

/* Components */
static button_t button;
static led_t led;
static passive_buzzer_t buzzer;
static tpl5010_t tpl5010;
static i2c_master_bus_handle_t i2c_bus_handle;
static at24cs0x_t at24cs02;
static fsm_t fsm;

/* FSM events */
static fsm_event_val_t is_prov = FSM_EVENT_VAL_NA;
static fsm_event_val_t is_ip = FSM_EVENT_VAL_NA;
static fsm_event_val_t is_ota = FSM_EVENT_VAL_NA;
static fsm_event_val_t is_full = FSM_EVENT_VAL_NA;

/* OTA variables */
#ifdef CONFIG_OTA_ENABLE
extern const uint8_t ota_cert[] asm("_binary_server_pem_start") ;
static char *ota_url = CONFIG_OTA_FILE_URL;
#endif /* CONFIG_OTA_ENABLE */

/* Buzzer sounds */
sound_t sound_beep[] = {
		{3000, 100, 100}
};

sound_t sound_warning[] = {
		{3000, 100, 100},
		{3000, 100, 0},
		{3000, 100, 100},
		{3000, 100, 0},
		{3000, 100, 100}
};

sound_t sound_success[] = {
		{4000, 300, 100},
		{2000, 200, 100},
		{3000, 300, 100}
};

sound_t sound_error[] = {
		{400, 300, 100},
		{200, 300, 100}
};

void boot_to_prov_fn(void);
void any_to_connected_fn(void);
void any_to_disconnected_fn(void);
void connected_to_full_fn(void);
void connected_to_ota_fn(void);

static fsm_row_t sst_list[8] = {
		{SYSTEM_STATE_INIT, SYSTEM_STATE_PROV, {{&is_prov, false}}, boot_to_prov_fn},
		{SYSTEM_STATE_INIT, SYSTEM_STATE_CONNECTED, {{&is_prov, true}, {&is_ip, true}}, any_to_connected_fn},
		{SYSTEM_STATE_INIT, SYSTEM_STATE_DISCONNECTED, {{&is_prov, true}, {&is_ip, false}}, any_to_disconnected_fn},
		{SYSTEM_STATE_CONNECTED, SYSTEM_STATE_DISCONNECTED, {{&is_ip, false}}, any_to_disconnected_fn},
		{SYSTEM_STATE_CONNECTED, SYSTEM_STATE_FULL, {{&is_full, true}}, connected_to_full_fn},
		{SYSTEM_STATE_CONNECTED, SYSTEM_STATE_OTA, {{&is_ota, true}}, connected_to_ota_fn},
		{SYSTEM_STATE_DISCONNECTED, SYSTEM_STATE_CONNECTED, {{&is_ip, true}}, any_to_connected_fn},
		{SYSTEM_STATE_FULL, SYSTEM_STATE_CONNECTED, {{&is_full, false}}, any_to_connected_fn}
};


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
static esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf,
		ssize_t inlen, uint8_t **outbuf, ssize_t * outlen, void *priv_data);

/* Utils */
static void erase_wifi_creds(void *arg);
static void firmware_update(void *arg);
static void print_dev_info(void);

/* RTOS tasks */
static void reconnect_wifi_task(void *arg);
static void fsm_task(void *arg);
static void clients_timeout_task(void *arg);

static char *read_http_response(httpd_req_t *req);
static esp_err_t settings_save_handler(httpd_req_t *req);
static esp_err_t settings_load_handler(httpd_req_t *req);
static esp_err_t login_handler(httpd_req_t *req);

static bool settings_load(void);
static bool settings_save(void);
static void settings_set_ssid(const char *ssid);
static void settings_set_clients(uint8_t clients);
static void settings_set_time(uint16_t time);
static char *settings_get_ssid(void);
static uint8_t settings_get_clients(void);
static uint16_t settings_get_time(void);

static void list_init(void);
static void list_add(uint8_t *mac);
static void list_remove(uint8_t *mac);

static esp_err_t spiffs_init(const char *base_path);

/* Main ----------------------------------------------------------------------*/
void app_main(void) {
	/* Initialize a LED instance */
	ESP_ERROR_CHECK(led_strip_init(
			&led,
			LED_PIN,
			2));

	/* Initialize FSM */
	fsm_init(&fsm, sst_list);
	LED_INIT_STATE();

	/* Initialize a button instance */
	ESP_ERROR_CHECK(button_init(
			&button,
			BUTTON_PIN,
			BUTTON_EDGE_FALLING,
			tskIDLE_PRIORITY + 4,
			configMINIMAL_STACK_SIZE * 2));

	button_add_cb(&button, BUTTON_CLICK_SINGLE, reset_device, NULL);
	button_add_cb(&button, BUTTON_CLICK_MEDIUM, firmware_update, NULL);
	button_add_cb(&button, BUTTON_CLICK_LONG, erase_wifi_creds, NULL);

	/* Initialize TPL5010 instance */
	ESP_ERROR_CHECK(tpl5010_init(
			&tpl5010,
			TPL5010_WAKE_PIN,
			TPL5010_DONE_PIN));

	/* Initialize a buzzer instance */
	passive_buzzer_init(
			&buzzer,
			BUZZER_PIN,
			LEDC_TIMER_0,
			LEDC_CHANNEL_0);

	/* Create FSM task */
	if (xTaskCreate(
			fsm_task,
			"FSM Task",
			configMINIMAL_STACK_SIZE * 3,
			(void *)&fsm,
			tskIDLE_PRIORITY + 5,
			NULL) != pdPASS) {
		ESP_LOGE(TAG, "Failed to create FreeRTOS task");
		error_handler();
	}

	/* Initialize I2C bus */
	i2c_master_bus_config_t i2c_bus_config = {
			.clk_source = I2C_CLK_SRC_DEFAULT,
			.i2c_port = I2C_NUM_0,
			.scl_io_num = I2C_BUS_SCL_PIN,
			.sda_io_num = I2C_BUS_SDA_PIN,
			.glitch_ignore_cnt = 7
	};

	ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle));

	/* Initialize AT24CS02 */
	ESP_ERROR_CHECK(at24cs0x_init(
			&at24cs02, /* AT24CS0X instance */
			i2c_bus_handle, /* I2C bus instance */
			AT24CS0X_I2C_ADDRESS, /* I2C device address */
			AT24CS0X_MODEL_02)); /* I2C custom write function */

	/* Load device settings */
	settings_load();

	/* Initialize NVS */
	ESP_ERROR_CHECK(nvs_init());

	/* Initialize Wi-Fi */
	ESP_ERROR_CHECK(wifi_init());

	/* Get OTA data and print device info */
	print_dev_info();

	list_init();
	/* Create FSM task */
	if (xTaskCreate(
			clients_timeout_task,
			"Client Timeout Task",
			configMINIMAL_STACK_SIZE * 2,
			NULL,
			tskIDLE_PRIORITY + 1,
			NULL) != pdPASS) {
		ESP_LOGE(TAG, "Failed to create FreeRTOS task");
		error_handler();
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
	esp_netif_create_default_wifi_ap();

	/* Initialize Wi-Fi driver */
	wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
	esp_wifi_init(&wifi_init_config);

	/* Declare event handler instances for Wi-Fi and IP */
	esp_event_handler_instance_t instance_any_wifi;
	esp_event_handler_instance_t instance_got_ip;
	esp_event_handler_instance_t instance_any_prov;

	/* Register Wi-Fi, IP and SmartConfig event handlers */
	ret = esp_event_handler_instance_register(
			WIFI_EVENT,
			ESP_EVENT_ANY_ID,
			&wifi_event_handler,
			NULL,
			&instance_any_wifi);

	if (ret != ESP_OK) {
		return ret;
	}

	ret = esp_event_handler_instance_register(
			IP_EVENT,
			IP_EVENT_STA_GOT_IP,
			&ip_event_handler,
			NULL,
			&instance_got_ip);

	if (ret != ESP_OK) {
		return ret;
	}

	ret = esp_event_handler_instance_register(
			WIFI_PROV_EVENT,
			ESP_EVENT_ANY_ID,
			&prov_event_handler,
			NULL,
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
	wifi_config.ap.max_connection = settings_get_clients();

	if (!strcmp(settings_get_ssid(), SETTINGS_SSID_DEFAULT)) {
		char *ssid = get_device_service_name(CONFIG_WIFI_AP_SSID_PREFIX);
		settings_set_ssid(ssid);
		settings_save();
		free(ssid);
	}

	strcpy((char *)wifi_config.ap.ssid, settings_get_ssid());
	wifi_config.ap.ssid_len = strlen(settings.ssid);

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

	/* Check if are Wi-Fi credentials provisioned */
	bool provisioned = false;
	ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

	if (provisioned) {
		ESP_LOGI(TAG, "Already provisioned. Connecting to AP...");
		esp_wifi_connect();

		/* Initizalize and configure file system, server and custom DNS */
		ESP_ERROR_CHECK(spiffs_init(SPIFFS_BASE_PATH));
		server_init(SPIFFS_BASE_PATH);
		server_uri_handler_add("/login", HTTP_POST, login_handler);
		server_uri_handler_add("/set_settings", HTTP_POST, settings_save_handler);
		server_uri_handler_add("/get_settings", HTTP_POST, settings_load_handler);
		cdns_init(SPIFFS_BASE_PATH);

		/* Initialize NAT */
		ip_napt_enable(ipaddr_addr("192.168.4.1"), 1);
		ESP_LOGI (TAG, "NAT is enabled");

		is_prov = FSM_EVENT_VAL_SET;
	}
	else {
		is_prov = FSM_EVENT_VAL_CLEAR;
	}

	return ret;
}

/* Event handlers */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
		int32_t event_id, void *event_data)
{
	switch (event_id) {
	case WIFI_EVENT_STA_START: {
		ESP_LOGI(TAG, "WIFI_EVENT_STA_START");

		break;
	}

	case WIFI_EVENT_STA_CONNECTED: {
		ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED");
		break;
	}

	case WIFI_EVENT_STA_DISCONNECTED: {
		ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED");
		is_ip = FSM_EVENT_VAL_CLEAR;
		break;
	}

	case WIFI_EVENT_AP_STACONNECTED: {
		ESP_LOGI(TAG, "WIFI_EVENT_AP_STACONNECTED");

		wifi_event_ap_staconnected_t *event =
				(wifi_event_ap_staconnected_t*)event_data;

		/* If the station connected is not close to device, then the
		 * connection is rejected */
		wifi_sta_list_t sta;
		esp_wifi_ap_get_sta_list(&sta);

		for (uint8_t i = 0; i < sta.num; i++) {
			if (!strncmp((char *)sta.sta[i].mac, (char *)event->mac, 6)) {
				if (sta.sta[i].rssi <= CONFIG_APP_RSSI_THRESHOLD_JOIN) {
					esp_wifi_deauth_sta(event->aid);
				}
				else {
					BUZZER_SUCCESS();
					list_add(event->mac);
				}
			}
		}

		/* Set the next state according the number of stations connected */
		if (clients.num >= settings_get_clients()) {
			is_full = FSM_EVENT_VAL_SET;
		}

		break;
	}

	case WIFI_EVENT_AP_STADISCONNECTED: {
		ESP_LOGI(TAG, "WIFI_EVENT_AP_STADISCONNECTED");

		wifi_event_ap_stadisconnected_t *event =
						(wifi_event_ap_stadisconnected_t*)event_data;

		list_remove(event->mac);

		/* Set the next state according the number of stations connected */
		if (clients.num < settings_get_clients()) {
			is_full = FSM_EVENT_VAL_CLEAR;
		}

		break;
	}

	default:
		ESP_LOGI(TAG, "Other Wi-Fi event");
		break;
	}
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
		int32_t event_id, void *event_data)
{
	switch (event_id) {
	case IP_EVENT_STA_GOT_IP: {
		ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP");
		is_ip = FSM_EVENT_VAL_SET;
		break;
	}

	case IP_EVENT_STA_LOST_IP: {
		ESP_LOGI(TAG, "IP_EVENT_STA_LOST_IP");
		break;
	}

	default: {
		ESP_LOGI(TAG, "Other IP event");
		break;
	}
	}
}

static void prov_event_handler(void *arg, esp_event_base_t event_base,
		int32_t event_id, void *event_data)
{
	switch (event_id) {
	case WIFI_PROV_START: {
		ESP_LOGI(TAG, "WIFI_PROV_START");
		break;
	}

	case WIFI_PROV_CRED_RECV: {
		ESP_LOGI(TAG, "WIFI_PROV_CRED_RECV");
		break;
	}

	case WIFI_PROV_CRED_SUCCESS: {
		ESP_LOGI(TAG, "WIFI_PROV_CRED_SUCCESS");
		break;
	}

	case WIFI_PROV_END: {
		ESP_LOGI(TAG, "WIFI_PROV_END");
		reset_device(NULL);
		break;
	}

	case WIFI_PROV_CRED_FAIL: {
		ESP_LOGI(TAG, "WIFI_PROV_CRED_FAIL");
		erase_wifi_creds(NULL);
		break;
	}

	case WIFI_PROV_DEINIT: {
		ESP_LOGI(TAG, "WIFI_PROV_DEINIT");
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
	sprintf(name, "%s%02X%02X%02X", ssid_prefix, mac_addr[3], mac_addr[4], mac_addr[5]);

	return name;
}

static esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf,
		ssize_t inlen, uint8_t **outbuf, ssize_t * outlen, void *priv_data) {
    if (inbuf) {
    	ESP_LOGI(TAG, "Received data: %.*s", inlen, (char *)inbuf);
    }

    char response[] = "88cb3bdf-2735-425e-8d4c-5e4e23eb8bdc/data_out";
    *outbuf = (uint8_t *)strdup(response);

    if(*outbuf == NULL) {
        ESP_LOGE(TAG, "System out of memory");
        return ESP_ERR_NO_MEM;
    }

    *outlen = strlen(response) + 1; /* +1 for NULL terminating byte */

    return ESP_OK;
}

/* RTOS tasks */
static void reconnect_wifi_task(void *arg) {
	uint8_t reconnection_cnt = 0;
	TickType_t last_time_wake = xTaskGetTickCount();

	for (;;) {
		/* Try connecting to Wi-Fi router using stored credentials. If
		 * connection is successful then the task delete itself, in other cases
		 * this function is executed again
		 */
		ESP_LOGW(TAG, "Unable to connect. Retrying...");

		esp_wifi_connect();

		/* Reset the device if it is not possible to recconect to the AP */
		if (++reconnection_cnt >= 30) {
			reset_device(NULL);
		}

		/* Wait CONFIG_APP_RECONNECTION_TIME to try to reconnect */
		vTaskDelayUntil(&last_time_wake, pdMS_TO_TICKS(reconnection_time));
	}
}

static void fsm_task(void *arg) {
	fsm_t *fsm_inst = (fsm_t *)arg;

	for (;;) {
		fsm_run(fsm_inst);
		vTaskDelay(pdMS_TO_TICKS(100));
	}
}

static void clients_timeout_task(void *arg) {
	TickType_t last_wake = xTaskGetTickCount();

	for (;;) {
		for (uint8_t i = 0; i < clients.num; i++) {
			if (--clients.client[i].time == 0) {
				uint16_t aid = 0;
				esp_wifi_ap_get_sta_aid(clients.client[i].mac, &aid);
				esp_wifi_deauth_sta(aid);
				ESP_LOGW(TAG, MACSTR" timeout!", MAC2STR(clients.client[i].mac));
			}
		}

		vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
	}
}

/* Utils */
static void erase_wifi_creds(void *arg)
{
	/* Erase any stored Wi-Fi credential  */
	ESP_LOGI(TAG, "Erasing Wi-Fi credentials...");

	esp_err_t ret = ESP_OK;

	ret = nvs_erase_namespace("nvs.net80211");

	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to erase Wi-Fi credentials");
		return;
	}

	/* Set settings to factory values */
	settings_set_ssid(SETTINGS_SSID_DEFAULT);
	settings_set_time(SETTINGS_TIME_DEFAULT);
	settings_set_clients(SETTINGS_CLIENTS_DEFAULT);
	settings_save();


	ESP_LOGI(TAG, "Settings set to factory values");

	reset_device(NULL);
}

static void firmware_update(void *arg)
{
	is_ota = FSM_EVENT_VAL_SET;
}

static void print_dev_info(void)
{
	char *ap_prov_name = get_device_service_name(CONFIG_WIFI_PROV_SSID_PREFIX);

	at24cs0x_read_serial_number(&at24cs02, serial_number);

	ESP_LOGI("info",
			"%s,%02X%02X%02X%02X%02X%02X,%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
			ap_prov_name, mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3],
			mac_addr[4], mac_addr[5], serial_number[0], serial_number[1],
			serial_number[2], serial_number[3], serial_number[4],
			serial_number[5], serial_number[6], serial_number[7],
			serial_number[8], serial_number[9], serial_number[10],
			serial_number[11], serial_number[12], serial_number[13],
			serial_number[14], serial_number[15]);

	free(ap_prov_name);
}

void boot_to_prov_fn(void) {
	printf("\tINIT -> PROV\r\n");
	LED_PROV_STATE();

	ESP_LOGI(TAG, "Initializing provisioning...");

	/* Initialize provisioning */
	wifi_prov_mgr_config_t prov_config = {
			.scheme = wifi_prov_scheme_softap,
			.scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE
	};

	ESP_ERROR_CHECK(wifi_prov_mgr_init(prov_config));

	/* Create endpoint */
	wifi_prov_mgr_endpoint_create("custom-data");

	/* Get SoftAP SSID name */
	char *ap_prov_name = get_device_service_name(CONFIG_WIFI_PROV_SSID_PREFIX);
	ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(
			WIFI_PROV_SECURITY_1,
			NULL,
			ap_prov_name,
			NULL));
	free(ap_prov_name);

	/* Register previous created endpoint */
	wifi_prov_mgr_endpoint_register("custom-data", custom_prov_data_handler, NULL);
};

void any_to_connected_fn(void) {
	printf("\tANY -> CONNECTED\r\n");
	LED_CONNECTED_STATE();

	/* Delete task to reconnect to AP */
	if (reconnect_wifi_handle != NULL) {
		vTaskDelete(reconnect_wifi_handle);
		reconnect_wifi_handle = NULL;
	}
};

void any_to_disconnected_fn(void) {
	printf("\tANY -> DISCONNECTED\r\n");
	LED_DISCONNECTED_STATE();

	/* Create to reconnect to AP */
	if (reconnect_wifi_handle == NULL) {
		if (xTaskCreate(
				reconnect_wifi_task,
				"Reconnect Wi-Fi Task",
				configMINIMAL_STACK_SIZE * 2,
				NULL,
				tskIDLE_PRIORITY + 1,
				&reconnect_wifi_handle) != pdPASS) {
			ESP_LOGE(TAG, "Failed to create FreeRTOS task");
		}
	}
};

void connected_to_full_fn(void) {
	printf("\tCONNECTED -> FULL\r\n");
	LED_FULL_STATE();
};

void connected_to_ota_fn(void) {
	printf("\tCONNECTED -> OTA\r\n");
	LED_OTA_STATE();

#ifdef CONFIG_OTA_ENABLE
	cdns_deinit();

	ESP_LOGI(TAG, "Downloading firmware from %s...", ota_url);

	if (ota_update(ota_url, (char*)ota_cert, 60000) == ESP_OK) {
		BUZZER_SUCCESS();
		reset_device(NULL);
	} else {
		BUZZER_FAIL();
		reset_device(NULL);
	}
#else
	ESP_LOGWfas(TAG, "Firmware OTA updates are disabled");
#endif /* CONFIG_OTA_ENABLE */
};

static char *read_http_response(httpd_req_t *req)
{
    int remaining = req->content_len;
    char *buf = ((struct file_server_data *)req->user_ctx)->scratch;
    int received;

	while (remaining > 0) {
		ESP_LOGI("server", "Remaining size : %d", remaining);
		/* Receive the file part by part into a buffer */
		if ((received = httpd_req_recv(req, buf, MIN(remaining, SCRATCH_BUFSIZE))) <= 0) {
			if (received == HTTPD_SOCK_ERR_TIMEOUT) {
				/* Retry if timeout occurred */
				continue;
			}

			ESP_LOGE("server", "File reception failed!");
			/* Respond with 500 Internal Server Error */
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
			return NULL;
		}

		remaining -= received;
	}

	/* Truncate the response string */
	buf[req->content_len] = '\0';

	return buf;
}

static esp_err_t settings_save_handler(httpd_req_t *req)
{
    char otp_header[11];

    if (httpd_req_get_hdr_value_str(req, "Otp", otp_header, sizeof(otp_header)) == ESP_OK) {
        if (otp == (uint32_t)strtoul(otp_header, NULL, 10)) {
        	/* Get response */
        	char *buf = read_http_response(req);

        	settings_t new_settings;
			sscanf(buf, "%hhu,%hu,%31s", &new_settings.clients, &new_settings.time,
					new_settings.ssid);

			printf("buffer:%d,%d,%s\r\n", settings.clients, settings.time,
					settings.ssid);

			/* Write the new data in EEPROM */
			if (strlen(new_settings.ssid) > 4) {
				strcpy(settings.ssid, new_settings.ssid);
			}

			if (new_settings.clients <= 15) {
				settings.clients = new_settings.clients;
			}

			if (new_settings.time > 0) {
				settings.time = new_settings.time;
			}

			if (settings_save()) {
				/* Process the response */
				const char *resp_str = "success";
				httpd_resp_set_type(req, "text/plain");
				httpd_resp_send(req, resp_str, strlen(resp_str));
				reset_device(NULL);
			}

        }
        else {
        	httpd_resp_send_500(req);
        }
    }

    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t settings_load_handler(httpd_req_t *req)
{
	/* Check the OTP received */
    char otp_header[11];

    if (httpd_req_get_hdr_value_str(req, "Otp", otp_header, sizeof(otp_header)) == ESP_OK) {
        if (otp == (uint32_t)strtoul(otp_header, NULL, 10)) {
        	char resp_str[128];
			sprintf(resp_str, "%d,%d,%s", settings.clients, settings.time, settings.ssid);
			httpd_resp_set_type(req, "text/plain");
			httpd_resp_send(req, resp_str, strlen(resp_str));
        }
        else {
            httpd_resp_send_500(req);
        }
    }

    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t login_handler(httpd_req_t *req)
{
	/* Get response */
	char *password = read_http_response(req);

    if (password) {
        /* Check if the password is correct */
    	char password_auth[7];
    	sprintf(password_auth, "%02X%02X%02X", mac_addr[3], mac_addr[4], mac_addr[5]);

        if (!strcmp(password, password_auth)) {
            otp = esp_random();
			char resp_str[128];
			sprintf(resp_str, "%lu", otp);
			httpd_resp_set_type(req, "text/plain");
			httpd_resp_send(req, resp_str, strlen(resp_str));
        }
        else {
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

static bool settings_load(void)
{
	if (at24cs0x_read(&at24cs02, SETTINGS_EEPROM_ADDR, (uint8_t*)&settings,
			sizeof(settings_t)) != 0) {
		ESP_LOGE(TAG, "Failed to load settings");
		return false;
	}

	if (*((uint8_t *)&settings) == 0xFF) {
		ESP_LOGW(TAG, "EPPROM empty, writing default data...");

		settings_set_ssid(SETTINGS_SSID_DEFAULT);
		settings_set_clients(SETTINGS_CLIENTS_DEFAULT);
		settings_set_time(SETTINGS_TIME_DEFAULT);

		if (!settings_save()) {
			return false;
		}
	}

	ESP_LOGI(TAG, "Settings loaded successfully");

	return true;
}

static bool settings_save(void)
{
	if (at24cs0x_write(&at24cs02, SETTINGS_EEPROM_ADDR, (uint8_t*)&settings,
			sizeof(settings_t)) != 0) {
		ESP_LOGE(TAG, "Failed to save settings");
		return false;
	}

	ESP_LOGI(TAG, "Settings saved successfully");

	return true;
}

static void settings_set_ssid(const char *ssid)
{
	strcpy(settings.ssid, ssid);
}

static void settings_set_clients(uint8_t clients)
{
	settings.clients = clients;
}

static void settings_set_time(uint16_t time)
{
	settings.time = time;
}


static char *settings_get_ssid(void)
{
	return settings.ssid;
}

static uint8_t settings_get_clients(void)
{
	return settings.clients;
}

static uint16_t settings_get_time(void)
{
	return settings.time;
}

static void list_init(void)
{
	clients.num = 0;
	clients.client = NULL;
}

static void list_add(uint8_t *mac)
{
	ESP_LOGI(TAG, "Adding "MACSTR, MAC2STR(mac));

	/* Reallocate memory for the new client */
	clients.client = (client_t *)realloc(clients.client, (clients.num + 1) * sizeof(client_t));

	if (clients.client == NULL) {
		ESP_LOGE(TAG, "Failed to add new client to list");
		return;
	}

	/* Fill the new client data */
	strncpy((char *)clients.client[clients.num].mac, (char *)mac, 6);
	clients.client[clients.num].time = settings_get_time();

	/* Increase the clients number */
	clients.num++;
}

static void list_remove(uint8_t *mac)
{
	ESP_LOGW(TAG, "Removing "MACSTR, MAC2STR(mac));

	/* Search for the client with the same MAC address */
	uint8_t idx = 0;

	while (idx < clients.num) {
		if (!strncmp((char *)clients.client[idx].mac, (char *)mac, 6)) {
			/* Shift the clients after the removed index */
			for (uint8_t i = idx; i < clients.num - 1; i++) {
				clients.client[i] = clients.client[i + 1];
			}

			/* Reduce the clients number */
			clients.num--;

			/* Reallocate memory */
			clients.client = (client_t *)realloc(clients.client, clients.num * sizeof(client_t));

			return;
		}

		idx++;
	}
}

static esp_err_t spiffs_init(const char *base_path)
{
	ESP_LOGI("server", "Initializing SPIFFS");

	esp_err_t ret = ESP_OK;

	esp_vfs_spiffs_conf_t conf = {
			.base_path = base_path,
			.partition_label = NULL,
			.max_files = 10,
			.format_if_mount_failed = false
	};

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

/***************************** END OF FILE ************************************/

