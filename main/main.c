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

#include "driver/gpio.h"

#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/lwip_napt.h"

#include "button.h"
#include "wifi.h"
#include "ws2812_led.h"


/* macros --------------------------------------------------------------------*/

#define MY_DNS_IP_ADDR 		0x08080808	/* 8.8.8.8 */
#define RSSI_THRESHOLD		-25

/* typedef -------------------------------------------------------------------*/

/* data declaration ----------------------------------------------------------*/

/* Tag for debugging */
static const char * TAG = "app";

/* Components instances */
static wifi_t wifi;
static button_t button;

/* Application variables */
static TaskHandle_t tryToReconnectHandle = NULL;
uint8_t updateLed = 0;

/* function declaration ------------------------------------------------------*/

static esp_err_t nvsInit(void);

/* FreeRTOS tasks */
static void wifiEventsTask(void * arg);
static void ipEventsTask(void * arg);
static void provEventsTask(void * arg);
static void buttonEventsTask(void * arg);
static void disconnectClientTask(void * arg);
static void tryToReconnectTask(void * arg);

/* main ----------------------------------------------------------------------*/

void app_main(void)
{
	ESP_LOGI(TAG, "Initializing components instances");

    /* Initialize NVS */
	ESP_ERROR_CHECK(nvsInit());
//	nvs_flash_erase();

	/* Initialize WS2812B LED */
	ESP_ERROR_CHECK(ws2812_led_init());
	ws2812_led_set_rgb(0, 63, 63);	/* Purple */

	/* Initialize Button instance */
	ESP_ERROR_CHECK(button_Init(&button));

	/* Initialize Wifi instance */
	ESP_ERROR_CHECK(wifi_Init(&wifi));

	/* Create FreeRTOS tasks */
	xTaskCreate(wifiEventsTask, "Wi-Fi Events Task", configMINIMAL_STACK_SIZE * 4, NULL, configMAX_PRIORITIES - 1, NULL);
	xTaskCreate(ipEventsTask, "IP Events Task", configMINIMAL_STACK_SIZE * 4, NULL, configMAX_PRIORITIES - 2, NULL);
	xTaskCreate(provEventsTask, "Provisioning Events Task", configMINIMAL_STACK_SIZE * 4, NULL, configMAX_PRIORITIES - 3, NULL);
	xTaskCreate(buttonEventsTask, "Button Events Task", configMINIMAL_STACK_SIZE * 4, NULL, tskIDLE_PRIORITY + 2, NULL);
	xTaskCreate(disconnectClientTask, "Disconnect Client Task", configMINIMAL_STACK_SIZE * 4, NULL, tskIDLE_PRIORITY + 1, NULL);
}

/* function definition -------------------------------------------------------*/

static void wifiEventsTask(void * arg) {
	EventBits_t bits;
	const EventBits_t bitsWaitFor = (WIFI_EVENT_STA_CONNECTED_BIT |
									 WIFI_EVENT_STA_DISCONNECTED_BIT |
									 WIFI_EVENT_AP_STACONNECTED_BIT);

	for(;;) {
		/* Wait until some bit is set */
		bits = xEventGroupWaitBits(wifi.wifiEventGroup, bitsWaitFor, pdTRUE, pdFALSE, portMAX_DELAY);

		if(bits & WIFI_EVENT_STA_CONNECTED_BIT) {
			/* Delete task to reconnect to AP */
			if(tryToReconnectHandle != NULL) {
				vTaskDelete(tryToReconnectHandle);
				tryToReconnectHandle = NULL;
			}
		}
		else if(bits & WIFI_EVENT_STA_DISCONNECTED_BIT) {
			/* Create task to reconnect to AP and set RGB led in blue color */
			if(tryToReconnectHandle == NULL) {
				xTaskCreate(tryToReconnectTask, "Try To Reconnect Task", configMINIMAL_STACK_SIZE * 3, NULL, tskIDLE_PRIORITY + 1, &tryToReconnectHandle);
			}

			updateLed = 0;

	        ws2812_led_set_rgb(0, 127, 0);	/* Red */
		}
		else if(bits & WIFI_EVENT_AP_STACONNECTED_BIT) {
			wifi_event_ap_staconnected_t * event = (wifi_event_ap_staconnected_t *)wifi.wifiEventData;

			ESP_LOGI(TAG, "station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);

			/* If the station connected is not close to ESP32-S2, then is connection is rejected */
			wifi_sta_list_t sta;

			esp_wifi_ap_get_sta_list(&sta);

			for(uint8_t i = 0; i < sta.num; i++) {
				ESP_LOGI(TAG, "station "MACSTR", RSSI: %d", MAC2STR(sta.sta[i].mac), sta.sta[i].rssi);

				if(!strncmp((const char *)sta.sta[i].mac, (const char *)event->mac, 6)) {
					if(sta.sta[i].rssi <= CONFIG_APP_RSSI_THRESHOLD_JOIN) {
						ESP_LOGI(TAG, "RSSI less than RSSI threshold");
						esp_wifi_deauth_sta(event->aid);
					}
				}
			}
		}
		else if(bits & WIFI_EVENT_AP_STADISCONNECTED_BIT) {

		}
		else {
			ESP_LOGI(TAG, "Other event");
		}
	}
}

static void ipEventsTask(void * arg) {
	EventBits_t bits;
	EventBits_t bitsWaitFor = (IP_EVENT_STA_GOT_IP_BIT);

	for(;;) {
		/* Wait until some bit is set */
		bits = xEventGroupWaitBits(wifi.ipEventGroup, bitsWaitFor, pdTRUE, pdFALSE, portMAX_DELAY);

		if(bits & IP_EVENT_STA_GOT_IP_BIT) {
			ip_event_got_ip_t * event = (ip_event_got_ip_t *)wifi.ipEventData;

			ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));

		    /* Initialize NAT */
		    ip_napt_enable ( htonl ( 0xC0A80401 ), 1 );	/* 192.168.4.1 */
		    ESP_LOGI ( TAG, "NAT is enabled" );

		    ws2812_led_set_rgb(127, 0, 0);	/* Green */

		    updateLed = 1;
		}
		else {
			ESP_LOGI(TAG, "Other event");
		}
	}
}

static void provEventsTask(void * arg) {
	EventBits_t bits;
	EventBits_t bitsWaitFor = (WIFI_PROV_START_BIT |
							   WIFI_PROV_CRED_RECV_BIT |
							   WIFI_PROV_CRED_FAIL_BIT |
							   WIFI_PROV_CRED_SUCCESS_BIT |
							   WIFI_PROV_END_BIT |
							   WIFI_PROV_DEINIT_BIT);

	for(;;) {
		/* Wait until some bit is set */
		bits = xEventGroupWaitBits(wifi.provEventGroup, bitsWaitFor, pdTRUE, pdFALSE, portMAX_DELAY);

		if(bits & WIFI_PROV_START_BIT) {
			ESP_LOGI(TAG, "Provisioning started");

			ws2812_led_set_rgb(0, 0, 127);	/* Blue */
		}
		else if(bits & WIFI_PROV_CRED_RECV_BIT) {
			wifi_sta_config_t * wifi_sta_cfg = (wifi_sta_config_t *)wifi.provEventData;
			ESP_LOGI(TAG, "Credentials received, SSID: %s & Password: %s", (const char *) wifi_sta_cfg->ssid, (const char *) wifi_sta_cfg->password);
		}
		else if(bits & WIFI_PROV_CRED_SUCCESS_BIT) {
			ESP_LOGI(TAG, "Provisioning successful");
		}
		else if(bits & WIFI_PROV_END_BIT) {
			/* De-initialize manager once provisioning is finished */
			wifi_prov_mgr_deinit();
		}
		else if(bits & WIFI_PROV_CRED_FAIL_BIT) {
			esp_restart();	/* Restart the device */
		}
		else if(bits & WIFI_PROV_DEINIT_BIT) {
			char * ssid = CONFIG_WIFI_AP_SSID;
			ssid = malloc((strlen(CONFIG_WIFI_AP_SSID) + 7 + 1) * sizeof(* ssid));

			if(ssid == NULL) {
				ssid = CONFIG_WIFI_AP_SSID;
			}
			else {
			    uint8_t eth_mac[6];
			    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
			    sprintf(ssid, "%s_%02X%02X%02X", CONFIG_WIFI_AP_SSID, eth_mac[3], eth_mac[4], eth_mac[5]);
			}

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

			ESP_ERROR_CHECK(esp_wifi_stop());
			ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
			ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config_ap));
			ip_addr_t dnsserver;
		    dnsserver.u_addr.ip4.addr = htonl ( MY_DNS_IP_ADDR );
		    dhcps_offer_t dhcps_dns_value = OFFER_DNS;
		    dhcps_set_option_info ( 6, &dhcps_dns_value, sizeof( dhcps_dns_value ) );
		    dnsserver.type = IPADDR_TYPE_V4;
		    dhcps_dns_setserver ( &dnsserver );
			ESP_ERROR_CHECK(esp_wifi_start());
			ESP_ERROR_CHECK(esp_wifi_connect());

			free(ssid);
		}
		else {
			ESP_LOGI(TAG, "Other event");
		}
	}
}

static void buttonEventsTask(void * arg) {
	EventBits_t bits;
	const EventBits_t bitsWaitFor = (BUTTON_SHORT_PRESS_BIT |
									 BUTTON_MEDIUM_PRESS_BIT |
									 BUTTON_LONG_PRESS_BIT);

	for(;;)
	{
		/* Wait until some bit is set */
		bits = xEventGroupWaitBits(button.eventGroup, bitsWaitFor, pdTRUE, pdFALSE, portMAX_DELAY);

		if(bits & BUTTON_SHORT_PRESS_BIT)
			ESP_LOGI(TAG, "BUTTON_SHORT_PRESS_BIT set!");

		else if(bits & BUTTON_MEDIUM_PRESS_BIT)
		{
			ESP_LOGI(TAG, "BUTTON_MEDIUM_PRESS_BIT set!");

			/* Erase any stored Wi-Fi credential  */
			ESP_LOGI(TAG, "Erasing Wi-Fi credentials");

			esp_err_t ret;

			nvs_handle_t nvs_handle;
			ret = nvs_open("nvs.net80211", NVS_READWRITE, &nvs_handle);

			if(ret == ESP_OK)
				nvs_erase_all(nvs_handle);

			/* Close NVS */
			ret = nvs_commit(nvs_handle);
			nvs_close(nvs_handle);

			if(ret == ESP_OK)
				/* Restart device */
				esp_restart();
		}
		else if(bits & BUTTON_LONG_PRESS_BIT)
			ESP_LOGI(TAG, "BUTTON_LONG_PRESS_BIT set!");
		else
			ESP_LOGI(TAG, "Button unexpected Event");
	}
}

static esp_err_t nvsInit(void) {
    esp_err_t ret = nvs_flash_init();

    if(ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }

    return ret;
}

static void disconnectClientTask(void * arg) {
	wifi_sta_list_t sta;

//	gpio_config_t gpio;
//	gpio.intr_type = GPIO_INTR_DISABLE;
//	gpio.mode = GPIO_MODE_OUTPUT;
//	gpio.pin_bit_mask = (1ULL << GPIO_NUM_8);
//	gpio.pull_down_en = GPIO_PULLDOWN_DISABLE;
//	gpio.pull_up_en = GPIO_PULLUP_DISABLE;
//	gpio_config(&gpio);
//
//	uint8_t level = 0;

	for(;;) {
		esp_wifi_ap_get_sta_list(&sta);

		/* Ask for stations RSSI and drop out if that is less than
		 * CONFIG_APP_RSSI_THRESHOLD_DROP_OUT */
		for(uint8_t i = 0; i < sta.num; i++) {
			ESP_LOGI(TAG, "station "MACSTR", RSSI: %d", MAC2STR (sta.sta[i].mac), sta.sta[i].rssi);

			if(sta.sta[i].rssi <= CONFIG_APP_RSSI_THRESHOLD_DROP_OUT) {
				uint16_t aid = 0;
				esp_wifi_ap_get_sta_aid(sta.sta[i].mac, &aid);
				ESP_LOGI(TAG, "Good bye" MACSTR, MAC2STR(sta.sta[i].mac));
				esp_wifi_deauth_sta(aid);
			}
		}

		/* Change LED color according the station list */
		if(updateLed) {
			if(sta.num < CONFIG_WIFI_AP_MAX_STA_CONN) {
				ws2812_led_set_rgb(127, 0, 0);	/* Green */
			}
			else if(sta.num == CONFIG_WIFI_AP_MAX_STA_CONN) {
				ws2812_led_set_rgb(82, 127, 0);	/* Orange */
			}
		}

//		gpio_set_level(GPIO_NUM_8, 1);
//		vTaskDelay(pdMS_TO_TICKS(100));
//		gpio_set_level(GPIO_NUM_8, 0);

		/* Wait 3 seg to update */
		vTaskDelay(pdMS_TO_TICKS(3000));
	}
}

static void tryToReconnectTask(void * arg)
{
	TickType_t last_time_wake = xTaskGetTickCount();

	for(;;)
	{
		/* Try connecting to Wi-Fi router using stored credentials. If connection is successful
		 * then the task delete itself, in other cases this function is executed again*/
		ESP_LOGI(TAG, "Unable to connect. Retrying...");
		esp_wifi_connect();

		/* Wait 20 sec to try reconnecting */
		vTaskDelayUntil(&last_time_wake, pdMS_TO_TICKS(CONFIG_APP_RECONNECTION_TIME));
	}
}

/* end of file ---------------------------------------------------------------*/
