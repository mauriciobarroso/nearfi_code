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
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "nvs_flash.h"
#include "rom/ets_sys.h"

#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"

#include "lwip/opt.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/dns.h"
#include "lwip/lwip_napt.h"
#include "esp_netif.h"

#include "esp_rgb_led.h"
#include "button.h"
#include "passive_buzzer.c"
#include "at24cs0x.h"
#include "i2c_bus.h"
#include "tpl5010.h"

/* Macros --------------------------------------------------------------------*/
#define DNS_IP_ADDR	"8.8.8.8"
#define AP_IP_ADDR	"192.168.4.1"
#define OTA_URL			"https://getbit-fuota.s3.amazonaws.com/NearFi.bin"

#define BUZZER_SUCCESS()		passive_buzzer_run(&buzzer, sound_success, 3);
#define BUZZER_FAIL()				passive_buzzer_run(&buzzer, sound_warning, 5);
#define BUZZER_ERROR()			passive_buzzer_run(&buzzer, sound_error, 2);
#define BUZZER_BEEP()				passive_buzzer_run(&buzzer, sound_beep, 3);

#define LED_SET_RED()				esp_rgb_led_set(&led, 255, 0, 0)
#define LED_SET_GREEN()			esp_rgb_led_set(&led, 0, 255, 0)
#define LED_SET_BLUE()			esp_rgb_led_set(&led, 0, 0, 255)
#define LED_SET_YELLOW()		esp_rgb_led_set(&led, 255, 255, 0)
#define LED_SET_PURPLE()		esp_rgb_led_set(&led, 255, 0, 255)
#define LED_BLINK_RED()			esp_rgb_led_blink_start(&led, 500, 255, 0, 0)
#define LED_BLINK_GREEN()		esp_rgb_led_blink_start(&led, 500, 0, 255, 0)
#define LED_BLINK_BLUE()		esp_rgb_led_blink_start(&led, 500, 0, 0, 255)
#define LED_BLINK_YELLOW()	esp_rgb_led_blink_start(&led, 500, 255, 255, 0)
#define LED_BLINK_PURPLE()	esp_rgb_led_blink_start(&led, 500, 255, 0, 255)

/* Typedef -------------------------------------------------------------------*/
typedef enum {
	BOOT_STATE = 0,
	PROV_STATE,
	CONNECTED_STATE,
	DISCONNECTED_STATE,
	FULL_STATE,
	OTA_STATE,
	MAX_STATE
} system_state_e;

/* Private variables ---------------------------------------------------------*/
static const char *TAG = "NearFi";
static TaskHandle_t reconnect_wifi_handle = NULL;
static TaskHandle_t ota_update_handle = NULL;
static system_state_e current_state = MAX_STATE;
static system_state_e next_state = BOOT_STATE;

/* Components */
static button_t button;
static esp_rgb_led_t led;
static passive_buzzer_t buzzer;

/* Certificates */
extern const uint8_t server_cert_pem_start[] asm("_binary_server_pem_start");

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

/* Private function prototypes -----------------------------------------------*/
/* Initialization functions */
static esp_err_t nvs_init(void);
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
static void reset_device(void *arg);
static void ota_update(void *arg);
static void print_macs(void);

/* RTOS tasks */
static void reconnect_wifi_task(void *arg);
static void ota_update_task(void *arg);
static void led_control_task(void *arg);

static tpl5010_t tpl5010;

/* I2C functions */
i2c_bus_t i2c_bus;
at24cs0x_t at24cs0x;

/* Main ----------------------------------------------------------------------*/
void app_main(void) {
	/* Initialize a button instance */
	ESP_ERROR_CHECK(button_init(&button,
			GPIO_NUM_38,
			tskIDLE_PRIORITY + 4,
			configMINIMAL_STACK_SIZE * 4));

	button_register_cb(&button, SHORT_TIME, reset_device, NULL);
	button_register_cb(&button, MEDIUM_TIME, ota_update, NULL);
	button_register_cb(&button, LONG_TIME, erase_wifi_creds, NULL);

	/* Initialize TPL5010 instance */
	tpl5010_init(&tpl5010, GPIO_NUM_41, GPIO_NUM_42);

	/* Initialize a LED instance */
	ESP_ERROR_CHECK(esp_rgb_led_init(&led, GPIO_NUM_14, 2));

	/* Create LED contorl task */
	xTaskCreate(led_control_task,
			"LED control Task",
			configMINIMAL_STACK_SIZE * 2,
			NULL,
			tskIDLE_PRIORITY + 3,
			NULL);

	/* Initialize a buzzer instance */
	passive_buzzer_init(&buzzer, GPIO_NUM_1, LEDC_TIMER_0, LEDC_CHANNEL_0);

	/* Initialize NVS */
	ESP_ERROR_CHECK(nvs_init());

	/* Initialize Wi-Fi */
	ESP_ERROR_CHECK(wifi_init());

	/* Print MACs */
	print_macs();

	/* Initialize I2C bus */
	i2c_bus_init(&i2c_bus, I2C_NUM_0, GPIO_NUM_39, GPIO_NUM_40, false, false, 400000);
	at24cs0x_init(&at24cs0x, &i2c_bus, AT24CS0X_I2C_ADDRESS, NULL, NULL);

  for (uint8_t i = 0x80; i < 0x80 + AT24CS0X_SN_SIZE; i++) {
  	uint8_t data = 0;

  	int8_t rslt = at24cs0x_read_random(&at24cs0x, i, &data);

		if (rslt == I2C_BUS_OK) {
				printf("data[0x%02X]: 0x%02X\n", i, data);
		} else {
				printf("Error %d\n", rslt);
		}
  }

  uint8_t data[AT24CS0X_SN_SIZE];
  int8_t rslt = at24cs0x.i2c_dev->read(0x80, data, AT24CS0X_SN_SIZE, at24cs0x.i2c_dev);
	if (rslt == I2C_BUS_OK) {
		printf("serial number: ");
		for (uint8_t i = 0; i < AT24CS0X_SN_SIZE; i++) {
			printf("%02X", data[i]);
		}
		printf("\r\n");
	} else {
		printf("Error %d\n", rslt);
	}

	at24cs0x_read_serial_number(&at24cs0x);
	printf("serial number: ");
	for (uint8_t i = 0; i < AT24CS0X_SN_SIZE; i++) {
		printf("%02X", at24cs0x.serial_number[i]);
	}
	printf("\r\n");

	at24cs0x.i2c_dev->read(0x22, data, AT24CS0X_SN_SIZE, at24cs0x.i2c_dev);
	if (rslt == I2C_BUS_OK) {
		printf("serial number: ");
		for (uint8_t i = 0; i < AT24CS0X_SN_SIZE; i++) {
			printf("%02X", data[i]);
		}
		printf("\r\n");
	} else {
		printf("Error %d\n", rslt);
	}
}

/* Private function definition -----------------------------------------------*/
/* Initialization functions */
static esp_err_t nvs_init(void) {
	esp_err_t ret;

	ESP_LOGI(TAG, "Initializing NVS...");

	ret = nvs_flash_init();

	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ret = nvs_flash_erase();

		if (ret == ESP_OK) {
			ret = nvs_flash_init();

			if (ret != ESP_OK) {
				ESP_LOGE(TAG, "Error initializing NVS");

				return ret;
			}
		}
		else {
			ESP_LOGE(TAG, "Error erasing NVS");

			return ret;
		}
	}

	return ret;
}

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

  uint32_t dhcps_lease_time = 60;
  ret = esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_IP_ADDRESS_LEASE_TIME, &dhcps_lease_time, sizeof(dhcps_lease_time));
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
  ret = esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &dns_offer, sizeof(dns_offer));
  if (ret != ESP_OK) {
  	return ret;
  }

  ret = esp_netif_dhcps_start(ap_netif);
  if (ret != ESP_OK) {
  	return ret;
  }

	/* Initialize Wi-Fi driver*/
	wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
	esp_wifi_init(&wifi_config);

	/* Declare event handler instances for Wi-Fi and IP */
	esp_event_handler_instance_t instance_any_wifi;
	esp_event_handler_instance_t instance_got_ip;
	esp_event_handler_instance_t instance_any_prov;

	/* Register Wi-Fi, IP and SmartConfig event handlers */
	ret = esp_event_handler_instance_register(WIFI_EVENT,
			ESP_EVENT_ANY_ID,
			&wifi_event_handler,
			NULL,
			&instance_any_wifi);

	if (ret != ESP_OK) {
		return ret;
	}

	ret = esp_event_handler_instance_register(IP_EVENT,
			IP_EVENT_STA_GOT_IP,
			&ip_event_handler,
			NULL,
			&instance_got_ip);

	if (ret != ESP_OK) {
		return ret;
	}

	ret = esp_event_handler_instance_register(WIFI_PROV_EVENT,
			ESP_EVENT_ANY_ID,
			&prov_event_handler,
			NULL,
			&instance_any_prov);

	if (ret != ESP_OK) {
		/* todo: write log */
		return ret;
	}

  wifi_config_t wifi_config_ap = {
  		.ap = {
  				.ssid = CONFIG_WIFI_AP_SSID,
					.ssid_len = strlen(CONFIG_WIFI_AP_SSID),
					.channel = CONFIG_WIFI_AP_CHANNEL,
					.password = CONFIG_WIFI_AP_PASS,
					.max_connection = CONFIG_WIFI_AP_MAX_STA_CONN,
					.authmode = strlen(CONFIG_WIFI_AP_PASS) < 6 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK
  		},
  };

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
  wifi_prov_mgr_config_t prov_config = {
  		.scheme = wifi_prov_scheme_softap,
		.scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE
  };

  ESP_ERROR_CHECK(wifi_prov_mgr_init(prov_config));

  /* Check if are Wi-Fi credentials provisioned */
  bool provisioned = false;
	ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

	if (provisioned) {
		ESP_LOGI(TAG, "Already provisioned. Connecting to AP...");

		/* We don't need the manager as device is already provisioned,
		* so let's release it's resources */
		wifi_prov_mgr_deinit();

		/* Try connecting to Wi-Fi router using stored credentials */
		esp_wifi_connect();
	}
	else {
		ESP_LOGI(TAG, "Starting provisioning...");

		/* Create endpoint */
		wifi_prov_mgr_endpoint_create("custom-data");

		/* Get SoftAP SSID name */
		char * apNameProv = get_device_service_name("PROV_");
		ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1,
				strlen(CONFIG_WIFI_POP_PIN) > 1 ? CONFIG_WIFI_POP_PIN : NULL,
						apNameProv,
						NULL));
		free(apNameProv);

		/* Register previous created endpoint */
		wifi_prov_mgr_endpoint_register("custom-data", custom_prov_data_handler, NULL);
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

			/* Delete task to reconnect to AP */
			if (reconnect_wifi_handle != NULL) {
				vTaskDelete(reconnect_wifi_handle);
				reconnect_wifi_handle = NULL;
			}

			break;
		}

		case WIFI_EVENT_STA_DISCONNECTED: {
			ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED");

			next_state = DISCONNECTED_STATE;

			/* Create reconnect_wifi_task to reconnect the device to the AP */
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

			break;
		}

		case WIFI_EVENT_AP_STACONNECTED: {
			ESP_LOGI(TAG, "WIFI_EVENT_AP_STACONNECTED");

			/* If the station connected is not close to ESP32-S2, then the
			 * connection is rejected */
			wifi_sta_list_t sta;
			esp_wifi_ap_get_sta_list(&sta);


			for (uint8_t i = 0; i < sta.num; i++) {
				if (!strncmp((const char *)sta.sta[i].mac, (const char *)event->mac, 6)) {
					if(sta.sta[i].rssi <= (current_state == PROV_STATE? CONFIG_APP_RSSI_THRESHOLD_JOIN * 2 : CONFIG_APP_RSSI_THRESHOLD_JOIN)) {
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
				next_state = FULL_STATE;
			}

			break;
		}

		case WIFI_EVENT_AP_STADISCONNECTED: {
			ESP_LOGI(TAG, "WIFI_EVENT_AP_STADISCONNECTED");

			/* Set the next state according the number of stations connected */
			wifi_sta_list_t sta;
			esp_wifi_ap_get_sta_list(&sta);

			if (sta.num < CONFIG_WIFI_AP_MAX_STA_CONN && current_state == FULL_STATE) {
				next_state = CONNECTED_STATE;
			}

			break;
		}

		default:
			ESP_LOGI(TAG, "Other Wi-Fi event");
			break;
	}
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
		int32_t event_id, void *event_data) {
	switch (event_id) {
		case IP_EVENT_STA_GOT_IP: {
			ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP");

			/* Initialize NAT */
			ip_napt_enable(ipaddr_addr(AP_IP_ADDR), 1);
			ESP_LOGI (TAG, "NAT is enabled");

			next_state = CONNECTED_STATE;
			break;
		}

		default: {
			ESP_LOGI(TAG, "Other IP event");

			break;
		}
	}
}

static void prov_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
	switch (event_id) {
		case WIFI_PROV_START: {
			ESP_LOGI(TAG, "WIFI_PROV_START");
			next_state = PROV_STATE;
			break;
		}

		case WIFI_PROV_CRED_RECV: {
			ESP_LOGI(TAG, "WIFI_PROV_CRED_RECV");

			wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
			ESP_LOGI(TAG, "Credentials received, SSID: %s & Password: %s", (const char *) wifi_sta_cfg->ssid, (const char *) wifi_sta_cfg->password);

			break;
		}

		case WIFI_PROV_CRED_SUCCESS: {
			ESP_LOGI(TAG, "WIFI_PROV_CRED_SUCCESS");

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
							.channel = 1,
							.password = "",
							.max_connection = 10,
							.authmode = WIFI_AUTH_OPEN
					},
			};

			strcpy((char *)wifi_config_ap.ap.ssid, ssid);

			/* Stop Wi-Fi and configure AP */
			ESP_ERROR_CHECK(esp_wifi_stop());
			ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
			ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config_ap));

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
		ESP_LOGI(TAG, "Unable to connect. Retrying...");

		esp_wifi_connect();

		/* Wait CONFIG_WIFI_RECONNECT_TIME to try to reconnect */
		vTaskDelayUntil(&last_time_wake, pdMS_TO_TICKS(15000));
	}
}

static void ota_update_task(void *arg) {
	for (;;) {
		ESP_LOGI(TAG, "Starting OTA updates...");

		next_state = OTA_STATE;

		esp_http_client_config_t http_client_config = {
				.url = OTA_URL,
				.cert_pem = (char *)server_cert_pem_start,
				.timeout_ms = 50000,
				.keep_alive_enable = true
		};

		esp_https_ota_config_t ota_config = {
				.http_config = &http_client_config,
		};

		/* If the update was successful restart the device */
		if (esp_https_ota(&ota_config) == ESP_OK) {
			ESP_LOGI(TAG, "Firmware update successful");
			reset_device(NULL);
		}
		else {
			ESP_LOGE(TAG, "Error updating firmware");
			BUZZER_ERROR();
		}

		/* Delete task */
		ota_update_handle = NULL;
		vTaskDelete(NULL);
	}
}

/* Utils */
static void erase_wifi_creds(void * arg) {
	/* Activate buzzer */
//	BUZZER_ERROR();

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
		BUZZER_ERROR();
		return;
	}
	else {
		/* Restart device */
		reset_device(NULL);
	}
}

static void reset_device(void *arg) {
	ESP_LOGI(TAG, "Restarting device...");
	BUZZER_FAIL();
	vTaskDelay(pdMS_TO_TICKS(600));
	esp_restart();
}

static void ota_update(void *arg) {
	/* Create ota_update_task to perform OTA updates */
	if (current_state == CONNECTED_STATE || current_state == FULL_STATE) {
		if (NULL == ota_update_handle) {
			if (xTaskCreate(ota_update_task,
					"OTA Updates Task",
					configMINIMAL_STACK_SIZE * 6,
					NULL,
					tskIDLE_PRIORITY + 5,
					&ota_update_handle) != pdPASS) {
				ESP_LOGI(TAG, "Error creating task");
				for (;;);	/* todo: implement error handler */
			}
		}
		else {
			ESP_LOGW(TAG, "Task already created");
		}
	}
}

static void led_control_task(void *arg) {
	for (;;) {
		if (next_state != current_state) {
			current_state = next_state;
			esp_rgb_led_blink_stop(&led);

			switch (current_state) {
				case BOOT_STATE: {
					printf("BOOT_STATE\n");
					LED_BLINK_PURPLE();
					break;
				}

				case PROV_STATE: {
					printf("PROV_STATE\n");
					LED_BLINK_BLUE();
					break;
				}

				case CONNECTED_STATE: {
					LED_SET_GREEN();
					printf("CONNECTED_STATE\n");

					break;
				}

				case DISCONNECTED_STATE: {
					printf("DISCONNECTED_STATE\n");
					LED_SET_RED();
					break;
				}

				case FULL_STATE:
					printf("FULL_STATE\n");
					LED_BLINK_GREEN();

					break;

				case OTA_STATE: {
					printf("OTA_STATE\n");
					LED_SET_YELLOW();
					break;
				}

				default:
					break;
			}
		}

		/* Wait for 100 ms */
		vTaskDelay(pdMS_TO_TICKS(200));
	}
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

/***************************** END OF FILE ************************************/

