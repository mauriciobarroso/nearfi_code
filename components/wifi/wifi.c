/**
  ******************************************************************************
  * @file           : wifi.c
  * @author         : Mauricio Barroso Benavides
  * @date           : Jun 5, 2022
  * @brief          : todo: write brief
  ******************************************************************************
  * @attention
  *
  * MIT License
  *
  * Copyright (c) 2022 Mauricio Barroso Benavides
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
#include "wifi.h"

/* Private define ------------------------------------------------------------*/

/* Private macro -------------------------------------------------------------*/

/* External variables --------------------------------------------------------*/

/* Private typedef -----------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
static const char * TAG = "wifi";

/* Private function prototypes -----------------------------------------------*/
/* Provisioning utils */
static esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t * inbuf, ssize_t inlen, uint8_t * * outbuf, ssize_t * outlen, void * priv_data);
static char * get_device_service_name(const char * ssid_prefix);

/* Event handlers */
static void wifi_event_handler(void * arg, esp_event_base_t event_base, int event_id, void * event_data);
static void ip_event_handler(void * arg, esp_event_base_t event_base, int event_id, void * event_data);
static void prov_event_handler(void * arg, esp_event_base_t event_base, int event_id, void * event_data);

/* Exported functions --------------------------------------------------------*/
esp_err_t wifi_init(wifi_t * const me) {
	ESP_LOGI(TAG, "Initializing wifi component...");

    /* Initialize stack TCP/IP */
    ESP_ERROR_CHECK(esp_netif_init());

    /* Create event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Create netif instances */
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    /* Register Wi-Fi event handler */
    if(me->wifi_event_handler == NULL) {
    	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
    			wifi_event_handler,
				(void *)me->wifi_event_data,
				NULL));
    }
    else {
    	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
    			me->wifi_event_handler,
				(void *)me->wifi_event_data,
				NULL));
    }

    /* Register IP event handler */
    if(me->wifi_event_handler == NULL) {
    	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
    			ip_event_handler,
				(void *)me->ip_event_data,
				NULL));
    }
    else {
    	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
    			ESP_EVENT_ANY_ID,
				me->ip_event_handler,
				(void *)me->ip_event_data,
				NULL));
    }

    /* Initialize Wi-Fi */
    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_config));

#if defined(CONFIG_WIFI_AP) || defined(CONFIG_WIFI_APSTA)
    wifi_config_t wifi_config_ap = {
    		.ap = {
    				.ssid = CONFIG_WIFI_AP_SSID,
					.ssid_len = strlen(CONFIG_WIFI_AP_SSID),
					.channel = CONFIG_WIFI_AP_CHANNEL,
					.password = CONFIG_WIFI_AP_PASS,
					.max_connection = CONFIG_WIFI_AP_MAX_STA_CONN,
					.authmode = WIFI_AUTH_WPA_WPA2_PSK
            },
    };

    if(strlen(CONFIG_WIFI_AP_PASS) == 0) {
    	wifi_config_ap.ap.authmode = WIFI_AUTH_OPEN;
    }
#endif

    /* Set Wi-Fi mode */
#if defined(CONFIG_WIFI_AP)
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config_ap));
#elif defined(CONFIG_WIFI_STA)
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
#elif defined(CONFIG_WIFI_APSTA)
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config_ap));
#endif

    /* Start Wi-Fi */
	ESP_ERROR_CHECK(esp_wifi_start());

    /* Initialize provisioning manager */
#ifdef CONFIG_WIFI_PROV_ENABLE
	/* Register provisioning envent handler */
    if(me->wifi_event_handler == NULL) {
    	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_PROV_EVENT,
    			ESP_EVENT_ANY_ID,
				prov_event_handler,
				(void *)me->prov_event_data,
				NULL));
    }
    else {
    	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_PROV_EVENT,
    			ESP_EVENT_ANY_ID,
				me->prov_event_handler,
				(void *)me->prov_event_data,
				NULL));
    }

    wifi_prov_mgr_config_t prov_config = {
    		.scheme = wifi_prov_scheme_softap,
			.scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE
    };

    ESP_ERROR_CHECK(wifi_prov_mgr_init(prov_config));

    /* Check if are Wi-Fi credentials provisioned */
    bool provisioned = false;
	ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

	if(provisioned) {
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
#endif

	return ESP_OK;
}

/* Private functions ---------------------------------------------------------*/
/* Provisioning utils */
static char * get_device_service_name(const char * ssid_prefix) {
	char * name = NULL;

	name = malloc((strlen(ssid_prefix) + 6 + 1) * sizeof(* name));

    uint8_t eth_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    sprintf(name, "%s%02X%02X%02X", ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);

    return name;
}

static esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t * inbuf, ssize_t inlen, uint8_t ** outbuf, ssize_t * outlen, void * priv_data) {
    if(inbuf) {
    	ESP_LOGI(TAG, "Received data: %.*s", inlen, (char *)inbuf);
    }

    char response[] = "SUCCESS";
    * outbuf = (uint8_t *)strdup(response);

    if(* outbuf == NULL) {
        ESP_LOGE(TAG, "System out of memory");
        return ESP_ERR_NO_MEM;
    }

    * outlen = strlen(response) + 1; /* +1 for NULL terminating byte */

    return ESP_OK;
}

/* Event handlers */
static void wifi_event_handler(void * arg, esp_event_base_t event_base, int event_id, void * event_data) {
	/* Keep empty */
}

static void ip_event_handler(void * arg, esp_event_base_t event_base, int event_id, void * event_data) {
	/* Keep empty */
}

static void prov_event_handler(void * arg, esp_event_base_t event_base, int event_id, void * event_data) {
	/* Keep empty */
}

/***************************** END OF FILE ************************************/
