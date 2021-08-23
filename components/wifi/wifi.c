/*
 * bitec_wifi.c
 *
 * Created on: Mar 23, 2021
 * Author: Mauricio Barroso Benavides
 */

/* inclusions ----------------------------------------------------------------*/

#include "include/wifi.h"

/* macros --------------------------------------------------------------------*/

/* typedef -------------------------------------------------------------------*/

/* internal data declaration -------------------------------------------------*/

static const char * TAG = "Wifi";

/* external data declaration -------------------------------------------------*/

/* internal functions declaration --------------------------------------------*/

/* Provisioning utilities */
static esp_err_t customProvDataHandler(uint32_t session_id, const uint8_t * inbuf, ssize_t inlen, uint8_t * * outbuf, ssize_t * outlen, void * priv_data);
static char * getDeviceServiceName(const char * ssid_prefix);

/* Event handlers */
static void wifiEventHandler(void * arg, esp_event_base_t event_base, int event_id, void * event_data);
static void ipEventHandler(void * arg, esp_event_base_t event_base, int event_id, void * event_data);
static void provEventHandler(void * arg, esp_event_base_t event_base, int event_id, void * event_data);

/* external functions definition ---------------------------------------------*/

esp_err_t wifi_Init(wifi_t * const me) {
	/* Create Wi-Fi event group */
	me->wifiEventGroup = xEventGroupCreate();

	if(me->wifiEventGroup == NULL) {
		ESP_LOGI(TAG, "Event group failed");
	}
	else {
		ESP_LOGI(TAG, "Event group successful");
	}


	/* Create IP event group */
	me->ipEventGroup = xEventGroupCreate();

	if(me->ipEventGroup == NULL) {
		ESP_LOGI(TAG, "Event group failed");
	}
	else {
		ESP_LOGI(TAG, "Event group successful");
	}

	/* Create provisioning event group */
	me->provEventGroup = xEventGroupCreate();

	if(me->provEventGroup == NULL) {
		ESP_LOGI(TAG, "Event group failed");
	}
	else {
		ESP_LOGI(TAG, "Event group successful");
	}

    /* Initialize stack TCP/IP */
    ESP_ERROR_CHECK(esp_netif_init());

    /* Create event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Create netif instances */
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    /* Register event handlers */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, me->wifiEventHandler, (void *)me, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, me->ipEventHandler, (void *)me, NULL));

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
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, me->provEventHandler, (void *)me, NULL));

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
		ESP_LOGI(TAG, "Starting provisioning");

		/* Create endpoint */
		wifi_prov_mgr_endpoint_create("custom-data");

		/* Get SoftAP SSID name */
		char * apNameProv = getDeviceServiceName("PROV_");
		ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, strlen(CONFIG_WIFI_POP_PIN) > 1 ? CONFIG_WIFI_POP_PIN : NULL, apNameProv, NULL));
		free(apNameProv);

		/* Register previous created endpoint */
		wifi_prov_mgr_endpoint_register("custom-data", customProvDataHandler, NULL);
	}
#endif

	return ESP_OK;
}

/* internal functions definition ---------------------------------------------*/

static char * getDeviceServiceName(const char * ssid_prefix) {
	char * name = NULL;

	name = malloc((strlen(ssid_prefix) + 6 + 1) * sizeof(* name));

    uint8_t eth_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    sprintf(name, "%s%02X%02X%02X", ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);

    return name;
}

static esp_err_t customProvDataHandler(uint32_t session_id, const uint8_t * inbuf, ssize_t inlen, uint8_t * * outbuf, ssize_t * outlen, void * priv_data) {
    if (inbuf)
    	ESP_LOGI(TAG, "Received data: %.*s", inlen, (char *)inbuf);
    char response[] = "SUCCESS";
    * outbuf = (uint8_t *)strdup(response);
    if (* outbuf == NULL) {
        ESP_LOGE(TAG, "System out of memory");
        return ESP_ERR_NO_MEM;
    }

    * outlen = strlen(response) + 1; /* +1 for NULL terminating byte */

    return ESP_OK;
}

static void wifiEventHandler(void * arg, esp_event_base_t event_base, int event_id, void * event_data) {
	wifi_t * wifi = (wifi_t *)arg;
	wifi->wifiEventData = event_data;

	switch(event_id)
	{
	case WIFI_EVENT_WIFI_READY:
		ESP_LOGI(TAG, "WIFI_EVENT_WIFI_READY_BIT");
		xEventGroupSetBits(wifi->wifiEventGroup, WIFI_EVENT_WIFI_READY_BIT);
		break;

	case WIFI_EVENT_SCAN_DONE:
		ESP_LOGI(TAG, "WIFI_EVENT_SCAN_DONE_BIT");
		xEventGroupSetBits(wifi->wifiEventGroup, WIFI_EVENT_SCAN_DONE_BIT);
		break;

	case WIFI_EVENT_STA_START:
		ESP_LOGI(TAG, "WIFI_EVENT_STA_START_BIT");
		xEventGroupSetBits(wifi->wifiEventGroup, WIFI_EVENT_STA_START_BIT);
		break;

	case WIFI_EVENT_STA_STOP:
		ESP_LOGI(TAG, "WIFI_EVENT_STA_STOP_BIT");
		xEventGroupSetBits(wifi->wifiEventGroup, WIFI_EVENT_STA_STOP_BIT);
		break;

	case WIFI_EVENT_STA_CONNECTED:
		ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED_BIT");
		xEventGroupSetBits(wifi->wifiEventGroup, WIFI_EVENT_STA_CONNECTED_BIT);
		break;

	case WIFI_EVENT_STA_DISCONNECTED:
		ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED_BIT");
		xEventGroupSetBits(wifi->wifiEventGroup, WIFI_EVENT_STA_DISCONNECTED_BIT);
		break;

	case WIFI_EVENT_STA_AUTHMODE_CHANGE:
		ESP_LOGI(TAG, "WIFI_EVENT_STA_AUTHMODE_CHANGE_BIT");
		xEventGroupSetBits(wifi->wifiEventGroup, WIFI_EVENT_STA_AUTHMODE_CHANGE_BIT);
		break;

	case WIFI_EVENT_STA_WPS_ER_SUCCESS:
		ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_SUCCESS_BIT");
		xEventGroupSetBits(wifi->wifiEventGroup, WIFI_EVENT_STA_WPS_ER_SUCCESS_BIT);
		break;

	case WIFI_EVENT_STA_WPS_ER_FAILED:
		ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_FAILED_BIT");
		xEventGroupSetBits(wifi->wifiEventGroup, WIFI_EVENT_STA_WPS_ER_FAILED_BIT);
		break;

	case WIFI_EVENT_STA_WPS_ER_TIMEOUT:
		ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_TIMEOUT_BIT");
		xEventGroupSetBits(wifi->wifiEventGroup, WIFI_EVENT_STA_WPS_ER_TIMEOUT_BIT);
		break;

	case WIFI_EVENT_STA_WPS_ER_PIN:
		ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_PIN_BIT");
		xEventGroupSetBits(wifi->wifiEventGroup, WIFI_EVENT_STA_WPS_ER_PIN_BIT);
		break;

	case WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP:
		ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP_BIT");
		xEventGroupSetBits(wifi->wifiEventGroup, WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP_BIT);
		break;

	case WIFI_EVENT_AP_START:
		ESP_LOGI(TAG, "WIFI_EVENT_AP_START_BIT");
		xEventGroupSetBits(wifi->wifiEventGroup, WIFI_EVENT_AP_START_BIT);
		break;

	case WIFI_EVENT_AP_STOP:
		ESP_LOGI(TAG, "WIFI_EVENT_AP_STOP_BIT");
		xEventGroupSetBits(wifi->wifiEventGroup, WIFI_EVENT_AP_STOP_BIT);
		break;

	case WIFI_EVENT_AP_STACONNECTED:
		ESP_LOGI(TAG, "WIFI_EVENT_AP_STACONNECTED_BIT");
		wifi_event_ap_staconnected_t * event = (wifi_event_ap_staconnected_t *)wifi->wifiEventData;
		ESP_LOGI(TAG, "station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);

		xEventGroupSetBits(wifi->wifiEventGroup, WIFI_EVENT_AP_STACONNECTED_BIT);
		break;

	case WIFI_EVENT_AP_STADISCONNECTED:
		ESP_LOGI(TAG, "WIFI_EVENT_AP_STADISCONNECTED_BIT");
		xEventGroupSetBits(wifi->wifiEventGroup, WIFI_EVENT_AP_STADISCONNECTED_BIT);
		break;

	case WIFI_EVENT_AP_PROBEREQRECVED:
		ESP_LOGI(TAG, "WIFI_EVENT_AP_PROBEREQRECVED_BIT");
		xEventGroupSetBits(wifi->wifiEventGroup, WIFI_EVENT_AP_PROBEREQRECVED_BIT);
		break;

		default:
			break;
	}
}

static void ipEventHandler(void * arg, esp_event_base_t event_base, int event_id, void * event_data) {
	wifi_t * wifi = (wifi_t *)arg;
	wifi->ipEventData = event_data;

	switch(event_id) {
		case IP_EVENT_STA_GOT_IP:
			ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP");
			xEventGroupSetBits(wifi->ipEventGroup, IP_EVENT_STA_GOT_IP_BIT);
			break;

		case IP_EVENT_STA_LOST_IP:
			ESP_LOGI(TAG, "IP_EVENT_STA_LOST_IP");
			xEventGroupSetBits(wifi->ipEventGroup, IP_EVENT_STA_LOST_IP_BIT);
			break;

		case IP_EVENT_AP_STAIPASSIGNED:
			ESP_LOGI(TAG, "IP_EVENT_AP_STAIPASSIGNED");
			xEventGroupSetBits(wifi->ipEventGroup, IP_EVENT_AP_STAIPASSIGNED_BIT);
			break;

		case IP_EVENT_GOT_IP6:
			ESP_LOGI(TAG, "IP_EVENT_GOT_IP6");
			xEventGroupSetBits(wifi->ipEventGroup, IP_EVENT_GOT_IP6_BIT);
			break;

		case IP_EVENT_ETH_GOT_IP:
			ESP_LOGI(TAG, "IP_EVENT_ETH_GOT_IP");
			xEventGroupSetBits(wifi->ipEventGroup, IP_EVENT_ETH_GOT_IP_BIT);
			break;

		case IP_EVENT_PPP_GOT_IP:
			ESP_LOGI(TAG, "IP_EVENT_PPP_GOT_IP");
			xEventGroupSetBits(wifi->ipEventGroup, IP_EVENT_PPP_GOT_IP_BIT);
			break;

		case IP_EVENT_PPP_LOST_IP:
			ESP_LOGI(TAG, "IP_EVENT_PPP_LOST_IP");
			xEventGroupSetBits(wifi->ipEventGroup, IP_EVENT_PPP_LOST_IP_BIT);
			break;

		default:
			break;
	}
}

static void provEventHandler(void * arg, esp_event_base_t event_base, int event_id, void * event_data) {
	wifi_t * wifi = (wifi_t *)arg;
	wifi->provEventData = event_data;

	switch(event_id) {
	case WIFI_PROV_INIT:
		ESP_LOGI(TAG, "WIFI_PROV_INIT");
		xEventGroupSetBits(wifi->provEventGroup, WIFI_PROV_INIT_BIT);
		break;

	case WIFI_PROV_START:
		ESP_LOGI(TAG, "WIFI_PROV_START");
		xEventGroupSetBits(wifi->provEventGroup, WIFI_PROV_START_BIT);
		break;

	case WIFI_PROV_CRED_RECV:
		ESP_LOGI(TAG, "WIFI_PROV_CRED_RECV");
		xEventGroupSetBits(wifi->provEventGroup, WIFI_PROV_CRED_RECV_BIT);
		break;

	case WIFI_PROV_CRED_FAIL:
		ESP_LOGI(TAG, "WIFI_PROV_CRED_FAIL");
		xEventGroupSetBits(wifi->provEventGroup, WIFI_PROV_CRED_FAIL_BIT);
		break;

	case WIFI_PROV_CRED_SUCCESS:
		ESP_LOGI(TAG, "WIFI_PROV_CRED_SUCCESS");
		xEventGroupSetBits(wifi->provEventGroup, WIFI_PROV_CRED_SUCCESS_BIT);
		break;

	case WIFI_PROV_END:
		ESP_LOGI(TAG, "WIFI_PROV_END");
		xEventGroupSetBits(wifi->provEventGroup, WIFI_PROV_END_BIT);
		break;

	case WIFI_PROV_DEINIT:
		ESP_LOGI(TAG, "WIFI_PROV_DEINIT");
		xEventGroupSetBits(wifi->provEventGroup, WIFI_PROV_DEINIT_BIT);
		break;

		default:
			break;
	}
}

/* end of file ---------------------------------------------------------------*/
