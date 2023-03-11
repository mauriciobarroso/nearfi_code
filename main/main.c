/*
 * main.c
 *
 * Created on: Nov 12, 2020
 * Author: Mauricio Barroso Benavides
 */

/* inclusions ----------------------------------------------------------------*/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wps.h"
#include "esp_https_ota.h"

#include "driver/gpio.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/lwip_napt.h"

#include "button.h"
#include "esp_buzzer.h"
#include "wifi.h"
#include "ws2812_led.h"

#include "soc/efuse_reg.h"
#include "esp_efuse.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_partition.h"
#include "esp_flash_encrypt.h"
#include "esp_efuse_table.h"
/* macros --------------------------------------------------------------------*/

#define DNS_IP_ADDR		(0x08080808)	/* 8.8.8.8 */
#define GOOGLE_IP_ADDR	(0x8efa41e4)	/* 142.250.65.228 */
#define OTA_URL			"https://getbit-fuota.s3.amazonaws.com/nearfi.bin"

#define BUZZER_SUCCESS()	esp_buzzer_start(&buzzer, 350, 0, 1)
#define BUZZER_FAIL()			esp_buzzer_start(&buzzer, 50, 50, 2)
#define BUZZER_ERROR()		esp_buzzer_start(&buzzer, 50, 50, 4)

#if CONFIG_IDF_TARGET_ESP32
#define TARGET_CRYPT_CNT_EFUSE  ESP_EFUSE_FLASH_CRYPT_CNT
#define TARGET_CRYPT_CNT_WIDTH  7
#elif CONFIG_IDF_TARGET_ESP32S2
#define TARGET_CRYPT_CNT_EFUSE ESP_EFUSE_SPI_BOOT_CRYPT_CNT
#define TARGET_CRYPT_CNT_WIDTH  3
#endif
/* typedef -------------------------------------------------------------------*/

typedef enum {
	BOOT_STATE = 0,
	PROV_STATE,
	CONNECTED_STATE,
	DISCONNECTED_STATE,
	FULL_STATE,
	OTA_STATE,
} system_state_e;

/* data declaration ----------------------------------------------------------*/

/* Tag for debugging */
static const char * TAG = "app";

/* Components instances */
static wifi_t wifi;
static button_t button;
static esp_buzzer_t buzzer;

/* Application variables */
static TaskHandle_t reconnect_handle = NULL;
static system_state_e state = BOOT_STATE;

/* Certificates */
extern const uint8_t server_cert_pem_start[] asm("_binary_server_pem_start");

/* function declaration ------------------------------------------------------*/

/* Utils*/
static esp_err_t nvs_init(void);
static void print_macs(void);
static void erase_wifi_creds(void * arg);
static void reset_device(void * arg);

/* RTOS tasks */
static void reconnect_task(void * arg);
static void ota_task(void * arg);
static void led_control_task(void * arg);

/* Event handlers */
static void wifi_event_handler(void * arg, esp_event_base_t event_base, int32_t event_id, void * event_data);
static void ip_event_handler(void * arg, esp_event_base_t event_base, int32_t event_id, void * event_data);
static void prov_event_handler(void * arg, esp_event_base_t event_base, int32_t event_id, void * event_data);

/* main ----------------------------------------------------------------------*/

void app_main(void) {
	ESP_LOGI(TAG, "Initializing components instances");

    /* Initialize NVS */
	ESP_ERROR_CHECK(nvs_init());

	/* Initialize WS2812B LED */
	ESP_ERROR_CHECK(ws2812_led_init());

	/* Initialize Button instance */
	ESP_ERROR_CHECK(button_init(&button,
			GPIO_NUM_21,
			tskIDLE_PRIORITY + 10,
			configMINIMAL_STACK_SIZE * 6));
	button_register_cb(&button, SHORT_TIME, reset_device, NULL);
	button_register_cb(&button, MEDIUM_TIME, ota_task, NULL);
	button_register_cb(&button, LONG_TIME, erase_wifi_creds, NULL);

	/* Initialize Buzzer instance */
	ESP_ERROR_CHECK(esp_buzzer_init(&buzzer, GPIO_NUM_4));

	/* Initialize Wifi instance */
	wifi.wifi_event_handler= wifi_event_handler;
	wifi.ip_event_handler = ip_event_handler;
	wifi.prov_event_handler = prov_event_handler;
	ESP_ERROR_CHECK(wifi_init(&wifi));

	print_macs();

	/* Create RTOS tasks */
	ESP_LOGI(TAG, "Creating RTOS tasks...");

	xTaskCreate(led_control_task,
			"LED control Task",
			configMINIMAL_STACK_SIZE,
			NULL,
			tskIDLE_PRIORITY + 1,
			NULL);
}

/* function definition -------------------------------------------------------*/

/* Utils */
static esp_err_t nvs_init(void) {
	esp_err_t ret;

	const esp_partition_t * partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS, NULL);

	if(partition != NULL) {
		nvs_sec_cfg_t nvs_sec_cfg;

		if(nvs_flash_read_security_cfg(partition, &nvs_sec_cfg) != ESP_OK) {
			ESP_ERROR_CHECK(nvs_flash_generate_keys(partition, &nvs_sec_cfg));
		}

		/* Initialize secure NVS */
		ret = nvs_flash_secure_init(&nvs_sec_cfg);
	}
	else {
		return ESP_FAIL;
	}

	return ret;
}

static void print_macs(void) {
	uint8_t mac[6];

	/* Print station and soft-AP MAC addresses */
	ESP_LOGI(TAG, "*****************************");
	esp_wifi_get_mac(WIFI_IF_STA, mac);
	ESP_LOGI(TAG, "* Station MAC: %02X%02X%02X%02X%02X%02X *",
			mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	esp_wifi_get_mac(WIFI_IF_AP, mac);
	ESP_LOGI(TAG, "* Soft-AP MAC: %02X%02X%02X%02X%02X%02X *",
				mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	ESP_LOGI(TAG, "*****************************");
}

/* RTOS tasks */
static void reconnect_task(void * arg) {
	TickType_t last_time_wake = xTaskGetTickCount();

	for(;;) {
		/* Try connecting to Wi-Fi router using stored credentials. If
		 * connection is successful then the task delete itself, in other cases
		 * this function is executed again
		 */
		ESP_LOGI(TAG, "Unable to connect. Retrying...");
		esp_wifi_connect();

		/* Wait 20 sec to try reconnecting */
		vTaskDelayUntil(&last_time_wake, pdMS_TO_TICKS(CONFIG_APP_RECONNECTION_TIME));
	}
}

static void ota_task(void * arg) {
	for (;;) {
		state = OTA_STATE;

		ESP_LOGI(TAG, "Starting OTA update...");

		esp_http_client_config_t config = {
				.url = OTA_URL,
				.cert_pem = (char *)server_cert_pem_start,
		};

		esp_err_t ret = esp_https_ota(&config);

		if(ret == ESP_OK) {
			ESP_LOGI(TAG, "Firmware updated successful");
		}

		ESP_LOGI(TAG, "Restarting device...");
		esp_restart();
	}
}

static void led_control_task(void * arg) {
	TickType_t last_time_wake = xTaskGetTickCount();
	bool blink = 0;

	for(;;) {
		switch(state) {
			case BOOT_STATE: {
				ws2812_led_set_rgb(0, 0, 127);	/* Blue */
				break;
			}

			case PROV_STATE: {
				blink = !blink;

				if(blink) {
					ws2812_led_set_rgb(0, 0, 127);	/* Blue blink */
				}
				else {
					ws2812_led_clear();
				}

				break;
			}

			case CONNECTED_STATE: {
				ws2812_led_set_rgb(0, 127, 0);	/* Green */

				wifi_sta_list_t sta;
				esp_wifi_ap_get_sta_list(&sta);

				if (sta.num >= CONFIG_WIFI_AP_MAX_STA_CONN) {
					state = FULL_STATE;
				}

				break;
			}

			case DISCONNECTED_STATE: {
				ws2812_led_set_rgb(127, 0, 0);	/* Red */
				break;
			}

			case FULL_STATE:

				blink = !blink;

				if(blink) {
					ws2812_led_set_rgb(0, 127, 0);	/* Blue blink */
				}
				else {
					ws2812_led_clear();
				}

				wifi_sta_list_t sta;
				esp_wifi_ap_get_sta_list(&sta);

				if (sta.num < CONFIG_WIFI_AP_MAX_STA_CONN) {
					state = CONNECTED_STATE;
				}

				break;

			case OTA_STATE: {
				ws2812_led_set_rgb(127, 127, 0);	/* Yellow */

				break;
			}

			default:
				break;
		}

		/* Wait for 500 ms */
		vTaskDelayUntil(&last_time_wake, pdMS_TO_TICKS(300));
	}
}

/* Event handlers */
static void wifi_event_handler(void * arg, esp_event_base_t event_base, int32_t event_id, void * event_data) {
	wifi_event_ap_staconnected_t * event = (wifi_event_ap_staconnected_t *)event_data;

	switch(event_id) {
		case WIFI_EVENT_STA_CONNECTED: {
			ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED");

			if(reconnect_handle != NULL) {
				vTaskDelete(reconnect_handle);
				reconnect_handle = NULL;
			}

			break;
		}

		case WIFI_EVENT_STA_DISCONNECTED: {
			ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED");

			/* Create task to reconnect to AP and set RGB led in blue color */
			if(reconnect_handle == NULL) {
				xTaskCreate(reconnect_task, "Try To Reconnect Task", configMINIMAL_STACK_SIZE * 3, NULL, tskIDLE_PRIORITY + 1, &reconnect_handle);
			}

			state = DISCONNECTED_STATE;

			break;
		}

		case WIFI_EVENT_AP_STACONNECTED: {
			ESP_LOGI(TAG, "WIFI_EVENT_AP_STACONNECTED");

			ESP_LOGI(TAG, "station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);

			/* If the station connected is not close to ESP32-S2, then the
			 * connection is rejected */
			wifi_sta_list_t sta;
			esp_wifi_ap_get_sta_list(&sta);


			for (uint8_t i = 0; i < sta.num; i++) {
				ESP_LOGI(TAG, "station "MACSTR", RSSI: %d", MAC2STR(sta.sta[i].mac), sta.sta[i].rssi);
				ESP_LOGI(TAG, "list[%d]: "MACSTR"", i, MAC2STR(sta.sta[i].mac));
				ESP_LOGI(TAG, "event: "MACSTR"", MAC2STR(event->mac));

				if (!strncmp((const char *)sta.sta[i].mac, (const char *)event->mac, 6)) {
					if(sta.sta[i].rssi <= (state == PROV_STATE? CONFIG_APP_RSSI_THRESHOLD_JOIN * 2 : CONFIG_APP_RSSI_THRESHOLD_JOIN)) {
						ESP_LOGE(TAG, "RSSI less than RSSI threshold");
						esp_wifi_deauth_sta(event->aid);
					}
					else {
						BUZZER_SUCCESS();
					}
				}
			}

			break;
		}

		case WIFI_EVENT_AP_STADISCONNECTED:
			break;

		default:
			ESP_LOGI(TAG, "Other event");
			break;
	}
}

static void ip_event_handler(void * arg, esp_event_base_t event_base, int32_t event_id, void * event_data) {
	switch(event_id) {
		case IP_EVENT_STA_GOT_IP: {
			ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP");

			state = CONNECTED_STATE;

			/* Initialize NAT */
			ip_napt_enable(htonl(0xC0A80401), 1);	/* 192.168.4.1 */
			ESP_LOGI (TAG, "NAT is enabled");

			break;
		}
		default: {
			ESP_LOGI(TAG, "Other event");

			break;
		}
	}
}

static void prov_event_handler(void * arg, esp_event_base_t event_base, int32_t event_id, void * event_data) {
	switch(event_id) {
		case WIFI_PROV_START: {
			ESP_LOGI(TAG, "WIFI_PROV_START");

			state = PROV_STATE;

			break;
		}

		case WIFI_PROV_CRED_RECV: {
			ESP_LOGI(TAG, "WIFI_PROV_CRED_RECV");

			wifi_sta_config_t * wifi_sta_cfg = (wifi_sta_config_t *)event_data;
			ESP_LOGI(TAG, "Credentials received, SSID: %s & Password: %s", (const char *) wifi_sta_cfg->ssid, (const char *) wifi_sta_cfg->password);

			break;
		}

		case WIFI_PROV_CRED_SUCCESS: {
			ESP_LOGI(TAG, "WIFI_PROV_CRED_SUCCESS");

			BUZZER_SUCCESS();

			break;
		}

		case WIFI_PROV_END: {
			ESP_LOGI(TAG, "WIFI_PROV_END");

			/* De-initialize manager once provisioning is finished */
			wifi_prov_mgr_deinit();

			break;
		}

		case WIFI_PROV_CRED_FAIL: {
			ESP_LOGI(TAG, "WIFI_PROV_CRED_FAIL");

			BUZZER_FAIL();

			/* Erase any stored Wi-Fi credentials  */
			ESP_LOGI(TAG, "Erasing Wi-Fi credentials");

			esp_err_t ret;

			nvs_handle_t nvs_handle;
			ret = nvs_open("nvs.net80211", NVS_READWRITE, &nvs_handle);

			if(ret == ESP_OK) {
				nvs_erase_all(nvs_handle);
			}

			/* Close NVS */
			ret = nvs_commit(nvs_handle);
			nvs_close(nvs_handle);

			if(ret == ESP_OK) {
				/* Restart device */
				esp_restart();
			}

			break;
		}

		case WIFI_PROV_DEINIT: {
			ESP_LOGI(TAG, "WIFI_PROV_DEINIT");

			/* Get SSID of the user AP */
		    wifi_config_t wifi_conf;
			esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_conf);

			char * ssid = NULL;

			/* Allocate space for the complete Wi-Fi AP name.
			 * - +1: _ symbol
			 * - +6: MAC address
			 * - +1: \0 string terminator */
			ssid = malloc((strlen((char *)wifi_conf.sta.ssid) + 1 + 6 + 1) * sizeof(* ssid));

			if(ssid == NULL) {
				ssid = (char *)wifi_conf.sta.ssid;
			}
			else {
				uint8_t eth_mac[6];
				esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
				sprintf(ssid, "%s_%02X%02X%02X", wifi_conf.sta.ssid, eth_mac[3], eth_mac[4], eth_mac[5]);
			}

			/* Set Wi-Fi Ap parameters */
			wifi_config_t wifi_config_ap = {
					.ap = {
							.ssid_len = strlen(ssid),
							.channel = CONFIG_WIFI_AP_CHANNEL,
							.password = CONFIG_WIFI_AP_PASS,
							.max_connection = CONFIG_WIFI_AP_MAX_STA_CONN,
							.authmode = WIFI_AUTH_OPEN
					},
			};

			strcpy((char *)wifi_config_ap.ap.ssid, ssid);

			/* Stop Wi-Fi and configure AP */
			ESP_ERROR_CHECK(esp_wifi_stop());
			ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
			ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config_ap));

			/* Set and configure DNS */
			ip_addr_t dnsserver;
			dnsserver.u_addr.ip4.addr = htonl(DNS_IP_ADDR);
			dnsserver.type = IPADDR_TYPE_V4;
			dhcps_offer_t dhcps_dns_value = OFFER_DNS;
			dhcps_set_option_info(6, &dhcps_dns_value, sizeof(dhcps_dns_value));
			dhcps_dns_setserver(&dnsserver);

			/* Start Wi-Fi */
			ESP_ERROR_CHECK(esp_wifi_start());
			ESP_ERROR_CHECK(esp_wifi_connect());

			free(ssid);

			break;
		}
		default: {
			ESP_LOGI(TAG, "Other event");

			break;
		}
	}
}

/* Utils */
static void erase_wifi_creds(void * arg) {
	/* Activate buzzer */
	BUZZER_ERROR();

	/* Erase any stored Wi-Fi credential  */
	ESP_LOGI(TAG, "Erasing Wi-Fi credentials...");

	esp_err_t ret = ESP_OK;

	nvs_handle_t nvs_handle;
	ret = nvs_open("nvs.net80211", NVS_READWRITE, &nvs_handle);

	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Error opening NVS namespace");
		return;
	}
	nvs_erase_all(nvs_handle);

	/* Close NVS */
	ret = nvs_commit(nvs_handle);
	nvs_close(nvs_handle);

	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Error commiting the changes in NVS");
		return;
	}
	else {
		/* Restart device */
		ESP_LOGI(TAG, "Restarting device...");
		vTaskDelay(pdMS_TO_TICKS(500));
		esp_restart();
	}
}

static void reset_device(void * arg) {
	ESP_LOGI(TAG, "Restarting device...");
	esp_restart();
}

/* end of file ---------------------------------------------------------------*/
