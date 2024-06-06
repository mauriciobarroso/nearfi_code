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
#include "esp_event.h"
#include "esp_https_ota.h"
#include "esp_netif.h"
#include "driver/i2c_master.h"
#include "lwip/lwip_napt.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"

#include "esp_rgb_led.h"
#include "button.h"
#include "passive_buzzer.c"
#include "at24cs0x.h"
#include "tpl5010.h"
#include "fsm.h"

#include "misc.c"
#include "nvs.c"
#include "typedefs.h"

/* Macros --------------------------------------------------------------------*/
#define DNS_IP_ADDR			"8.8.8.8"
#define AP_IP_ADDR			"192.168.4.1"

#define BUZZER_SUCCESS()	passive_buzzer_run(&buzzer, sound_success, 3);
#define BUZZER_FAIL()		passive_buzzer_run(&buzzer, sound_warning, 5);
#define BUZZER_ERROR()		passive_buzzer_run(&buzzer, sound_error, 2);
#define BUZZER_BEEP()		passive_buzzer_run(&buzzer, sound_beep, 3);

#define LED_CONNECTED_STATE()		esp_rgb_led_set_continuos(&led, 0, 255, 0)
#define LED_INIT_STATE()			esp_rgb_led_set_continuos(&led, 255, 0, 255)
#define LED_DISCONNECTED_STATE()	esp_rgb_led_set_blink(&led, 255, 0, 0, 500, 500)
#define LED_PROV_STATE()			esp_rgb_led_set_fade(&led, 0, 0, 255, 1000, 1000)
#define LED_OTA_STATE()				esp_rgb_led_set_fade(&led, 255, 255, 0, 1000, 1000)
#define LED_FULL_STATE()			esp_rgb_led_set_continuos(&led, 255, 165, 0)

#define I2C_BUS_SDA_PIN		CONFIG_PERIPHERALS_I2C_SDA_PIN
#define I2C_BUS_SCL_PIN		CONFIG_PERIPHERALS_I2C_SCL_PIN
#define TPL5010_WAKE_PIN	CONFIG_PERIPHERALS_EWDT_WAKE_PIN
#define TPL5010_DONE_PIN	CONFIG_PERIPHERALS_EWDT_DONE_PIN
#define BUTTON_PIN			CONFIG_PERIPHERALS_BUTTON_PIN
#define BUZZER_PIN			CONFIG_PERIPHERALS_BUZZER_PIN
#define LED_PIN				CONFIG_PERIPHERALS_LEDS_PIN

/* Typedef -------------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
/* Tag for debug */
static const char *TAG = "NearFi";

static TaskHandle_t reconnect_wifi_handle = NULL;
static uint8_t serial_number[AT24CS0X_SN_SIZE];

/* Components */
static button_t button;
static esp_rgb_led_t led;
static passive_buzzer_t buzzer;
static tpl5010_t tpl5010;
static i2c_master_bus_handle_t i2c_bus_handle;
static at24cs0x_t at24cs02;
static fsm_t fsm;

/* OTA variables */
#ifdef CONFIG_OTA_ENABLE
extern const uint8_t ota_cert[] asm("_binary_server_pem_start");
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

/* System SST */
fsm_event_val_t is_prov = FSM_EVENT_VAL_NA;
fsm_event_val_t is_ip = FSM_EVENT_VAL_NA;
fsm_event_val_t is_ota = FSM_EVENT_VAL_NA;
fsm_event_val_t is_full = FSM_EVENT_VAL_NA;

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

/* Main ----------------------------------------------------------------------*/
void app_main(void) {
	/* Initialize a LED instance */
	ESP_ERROR_CHECK(esp_rgb_led_init(
			&led,
			LED_PIN,
			2));

	LED_INIT_STATE();

	/* Initialize FSM */
	fsm_init(&fsm, sst_list);

	xTaskCreate(
			fsm_task,
			"FSM Task",
			configMINIMAL_STACK_SIZE * 4,
			(void *)&fsm,
			tskIDLE_PRIORITY + 5,
			NULL);

	/* Initialize a button instance */
	ESP_ERROR_CHECK(button_init(&button,
			BUTTON_PIN,
			BUTTON_EDGE_FALLING,
			tskIDLE_PRIORITY + 4,
			configMINIMAL_STACK_SIZE * 4));

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
			AT24CS02_MODEL)); /* I2C custom write function */

	/* Initialize NVS */
	ESP_ERROR_CHECK(nvs_init());

	/* Initialize Wi-Fi */
	ESP_ERROR_CHECK(wifi_init());

	/* Get OTA data and print device info */
	print_dev_info();
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
	ret = esp_netif_dhcps_option(
			ap_netif,
			ESP_NETIF_OP_SET,
			ESP_NETIF_IP_ADDRESS_LEASE_TIME,
			&dhcps_lease_time,
			sizeof(dhcps_lease_time));

	if (ret != ESP_OK) {
		return ret;
	}

	esp_netif_dns_info_t dns_info = {0};
	dns_info.ip.u_addr.ip4.addr = ipaddr_addr(DNS_IP_ADDR);
	dns_info.ip.type = IPADDR_TYPE_V4;
	ret = esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns_info);
	if (ret != ESP_OK) {
		return ret;
	}

	uint8_t dns_offer = 1;
	ret = esp_netif_dhcps_option(
			ap_netif,
			ESP_NETIF_OP_SET,
			ESP_NETIF_DOMAIN_NAME_SERVER,
			&dns_offer,
			sizeof(dns_offer));

	if (ret != ESP_OK) {
		return ret;
	}

	ret = esp_netif_dhcps_start(ap_netif);
	if (ret != ESP_OK) {
		return ret;
	}

	/* Initialize Wi-Fi driver */
	wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
	esp_wifi_init(&wifi_config);

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
		/* todo: write log */
		return ret;
	}

	char *ssid = get_device_service_name(CONFIG_WIFI_AP_SSID);

	wifi_config_t wifi_config_ap = {
		.ap = {
				.ssid = CONFIG_WIFI_AP_SSID,
				.ssid_len = strlen(ssid),
				.channel = CONFIG_WIFI_AP_CHANNEL,
				.password = CONFIG_WIFI_AP_PASS,
				.max_connection = CONFIG_WIFI_AP_MAX_STA_CONN,
				.authmode = WIFI_AUTH_OPEN
		},
	};

	strcpy((char *)wifi_config_ap.ap.ssid, ssid);
	free(ssid);

	/* Set Wi-Fi mode and config */
	ret = esp_wifi_set_mode(WIFI_MODE_APSTA);

	if (ret != ESP_OK) {
		return ret;
	}

	ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_config_ap);

	if (ret != ESP_OK) {
		return ret;
	}

	/* Start Wi-Fi */
	ret = esp_wifi_start();

	if (ret != ESP_OK) {
		return ret;
	}

	/**/
	ret = esp_wifi_set_ps(WIFI_PS_NONE);

	if (ret != ESP_OK) {
		return ret;
	}

	/* Check if are Wi-Fi credentials provisioned */
	bool provisioned = false;
	ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

	if (provisioned) {
		is_prov = FSM_EVENT_VAL_SET;
		ESP_LOGI(TAG, "Already provisioned. Connecting to AP...");
		esp_wifi_connect();
	}
	else {
		is_prov = FSM_EVENT_VAL_CLEAR;
	}

	return ret;
}

/* Event handlers */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
							   int32_t event_id, void *event_data) {
	wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;

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

			/* If the station connected is not close to device, then the
			 * connection is rejected */
			wifi_sta_list_t sta;
			esp_wifi_ap_get_sta_list(&sta);

			for (uint8_t i = 0; i < sta.num; i++) {
				if (!strncmp((const char *)sta.sta[i].mac, (const char *)event->mac, 6)) {
					if(sta.sta[i].rssi <= CONFIG_APP_RSSI_THRESHOLD_JOIN) {
						ESP_LOGE(TAG, "RSSI less than RSSI threshold");
						esp_wifi_deauth_sta(event->aid);
					}
					else {
						BUZZER_SUCCESS();
					}
				}
			}

			/* Set the next state according the number of stations connected */
			if (sta.num >= CONFIG_WIFI_AP_MAX_STA_CONN) {
				is_full = FSM_EVENT_VAL_SET;
			}

			printf("clients connected: %d\r\n", sta.num);

			break;
		}

		case WIFI_EVENT_AP_STADISCONNECTED: {
			ESP_LOGI(TAG, "WIFI_EVENT_AP_STADISCONNECTED");

			/* Set the next state according the number of stations connected */
			wifi_sta_list_t sta;
			esp_wifi_ap_get_sta_list(&sta);

			if (sta.num < CONFIG_WIFI_AP_MAX_STA_CONN) {
				is_full = FSM_EVENT_VAL_CLEAR;
			}

			printf("clients connected: %d\r\n", sta.num);

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

	uint8_t eth_mac[6];
	esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
	sprintf(name, "%s%02X%02X%02X", ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);

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
	TickType_t last_time_wake = xTaskGetTickCount();

	for (;;) {
		/* Try connecting to Wi-Fi router using stored credentials. If
		 * connection is successful then the task delete itself, in other cases
		 * this function is executed again
		 */
		ESP_LOGW(TAG, "Unable to connect. Retrying...");

		esp_wifi_connect();

		/* Wait CONFIG_WIFI_RECONNECT_TIME to try to reconnect */
		vTaskDelayUntil(&last_time_wake, pdMS_TO_TICKS(15000));
	}
}

static void fsm_task(void *arg) {
	fsm_t *fsm_inst = (fsm_t *)arg;

	for (;;) {
		fsm_run(fsm_inst);
		vTaskDelay(pdMS_TO_TICKS(100));
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

	ESP_LOGI(TAG, "Wi-Fi credentials erased successfully");
	reset_device(NULL);
}

static void firmware_update(void *arg)
{
	is_ota = FSM_EVENT_VAL_SET;
}

static void print_dev_info(void)
{
	char *ap_prov_name = get_device_service_name(CONFIG_WIFI_PROV_SSID_PREFIX);

	uint8_t mac[6];
	esp_wifi_get_mac(WIFI_IF_STA, mac);

	at24cs0x_read_serial_number(&at24cs02, serial_number);

	ESP_LOGI("info",
			"%s,%02X%02X%02X%02X%02X%02X,%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
			ap_prov_name, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
			serial_number[0], serial_number[1], serial_number[2],
			serial_number[3], serial_number[4], serial_number[5],
			serial_number[6], serial_number[7], serial_number[8],
			serial_number[9], serial_number[10], serial_number[11],
			serial_number[12], serial_number[13], serial_number[14],
			serial_number[15]);

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
	ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1,
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

	/* Initialize NAT */
	ip_napt_enable(ipaddr_addr(AP_IP_ADDR), 1);
	ESP_LOGI (TAG, "NAT is enabled");

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
		if (xTaskCreate(reconnect_wifi_task,
				"Reconnect Wi-Fi Task",
				configMINIMAL_STACK_SIZE * 3,
				NULL,
				tskIDLE_PRIORITY + 1,
				&reconnect_wifi_handle) != pdPASS) {
			ESP_LOGI(TAG, "Error creating task");
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
	ESP_LOGI(TAG, "Downloading firmware from %s...", ota_url);

	if (ota_update(ota_url, (char*)ota_cert, 60000) == ESP_OK) {
		BUZZER_SUCCESS();
		reset_device(NULL);
	} else {
		BUZZER_FAIL();
		reset_device(NULL);
	}
#else
	ESP_LOGE(TAG, "Firmware OTA updates are disabled, enable via menuconfig");
	reset_device(NULL);
#endif /* CONFIG_OTA_ENABLE */
};

/***************************** END OF FILE ************************************/

