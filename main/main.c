/*
 * main.c
 *
 * Created on: Nov 12, 2020
 * Author: Mauricio Barroso Benavides
 */

/* inclusions ----------------------------------------------------------------*/

#include <string.h>
#include <ws2812_led.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_wps.h"
#include "esp_log.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/lwip_napt.h"

/* macros --------------------------------------------------------------------*/

#define SOFTAP_SSID			"myssid"
#define SOFTAP_PASS			""
#define SOFTAP_CHANNEL		1
#define SOFTAP_MAX_STA_CONN	4

#define MAXIMUM_RETRY		5

#define WIFI_CONNECTED_BIT	BIT0
#define WIFI_FAIL_BIT		BIT1

#define MY_DNS_IP_ADDR 		0x08080808 // 8.8.8.8

#define RSSI_THRESHOLD		-20

/* typedef -------------------------------------------------------------------*/

/* data declaration ----------------------------------------------------------*/

static const char *TAG = "wifi";
static int s_retry_num = 0;
static esp_wps_config_t wps_config = WPS_CONFIG_INIT_DEFAULT ( WPS_TYPE_PBC );
static wifi_config_t wps_ap_creds[MAX_WPS_AP_CRED];
static int s_ap_creds_num = 0;
static TaskHandle_t wps_task_handle;

/* function declaration ------------------------------------------------------*/

static void wifi_event_handler (void * arg, esp_event_base_t event_base, int32_t event_id, void * event_data);
static void ip_event_handler ( void * arg, esp_event_base_t event_base, int32_t event_id, void * event_data);
static void wifi_init_apsta ( void );
static void disconnect_task ( void * arg );
static void wps_task ( void * arg );
static void IRAM_ATTR give_notification ( void * arg );
static esp_err_t rgb_init ( void );

/* main ----------------------------------------------------------------------*/

void app_main(void)
{
    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init ();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND )
    {
      ESP_ERROR_CHECK(nvs_flash_erase () );
      ret = nvs_flash_init ();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI ( TAG, "ESP32-S2 Wi-Fi mode AP + STA" );
    wifi_init_apsta ();
    rgb_init ();

    /* Initialize NAT */
    ip_napt_enable ( htonl ( 0xC0A80401 ), 1 );	/* 192.168.4.1 */
    ESP_LOGI ( TAG, "NAT is enabled" );

    /* Create user tasks */
    xTaskCreate ( disconnect_task, "Disconnect Task", configMINIMAL_STACK_SIZE * 3, NULL, 3, NULL );
    xTaskCreate ( wps_task, "WPS Task", configMINIMAL_STACK_SIZE * 3, NULL, 2, &wps_task_handle );
}

/* function definition -------------------------------------------------------*/

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    switch ( event_id )
    {
    	case WIFI_EVENT_AP_START:
    	{
    		ESP_LOGI ( TAG, "WIFI_EVENT_AP_START" );
    		ESP_LOGI( TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
    		    		SOFTAP_SSID, SOFTAP_PASS, SOFTAP_CHANNEL );
    		break;
    	}

    	case WIFI_EVENT_AP_STACONNECTED:
    	{
    		ESP_LOGI ( TAG, "WIFI_EVENT_AP_STACONNECTED" );
			wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
			ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
					MAC2STR(event->mac), event->aid);

			/* If the station connected is not close to ESP32-S2, then is connection is rejected */
			wifi_sta_list_t sta = { 0 };
			esp_wifi_ap_get_sta_list (&sta);
			for ( uint8_t i = 0; i < sta.num; i++ ) {
				ESP_LOGI ( TAG, "station "MACSTR", RSSI: %d",
						MAC2STR( sta.sta [ i ].mac ),
						sta.sta [ i ].rssi );

				if ( !strncmp( ( const char * ) sta.sta [ i ].mac, ( const char * ) event->mac, 6 ) )
				{
					if ( sta.sta [ i ].rssi <= RSSI_THRESHOLD )
						esp_wifi_deauth_sta ( event->aid );
				}
			}
			break;
    	}

    	case WIFI_EVENT_AP_STADISCONNECTED:
    	{
    		ESP_LOGI ( TAG, "WIFI_EVENT_AP_STADISCONNECTED" );
            wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
            ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                     MAC2STR(event->mac), event->aid);
            break;
    	}

    	case WIFI_EVENT_STA_START:
    	{
    		ESP_LOGI ( TAG, "WIFI_EVENT_STA_START" );
            esp_wifi_connect();
        	break;
    	}

    	case WIFI_EVENT_STA_DISCONNECTED:
    	{
    		ESP_LOGI ( TAG, "WIFI_EVENT_STA_DISCONNECTED" );
    		wifi_event_sta_disconnected_t * event = ( wifi_event_sta_disconnected_t * ) event_data;
    		ESP_LOGI ( TAG, "reason: %d", event->reason );

    		ws2812_led_set_hsv ( 1, 100, 25 );

    		switch ( event->reason )
    		{
    			case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:

    			{
					break;
				}

    			case WIFI_REASON_ASSOC_LEAVE:
    			{
    				break;
				}

    			default:
    			{
					if (s_retry_num < MAXIMUM_RETRY)
					{
						esp_wifi_connect();
						s_retry_num++;
						ESP_LOGI(TAG, "retry to connect to the AP");
					}

					ESP_LOGI(TAG,"connect to the AP fail");

    				break;
    			}
    		}

        	break;
    	}

        case WIFI_EVENT_STA_WPS_ER_SUCCESS:
        {
            ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_SUCCESS");
            {
                wifi_event_sta_wps_er_success_t * event = ( wifi_event_sta_wps_er_success_t * ) event_data;
                int i;

                if (event) {
                    s_ap_creds_num = event->ap_cred_cnt;
                    for (i = 0; i < s_ap_creds_num; i++) {
                        memcpy(wps_ap_creds[i].sta.ssid, event->ap_cred[i].ssid,
                               sizeof(event->ap_cred[i].ssid));
                        memcpy(wps_ap_creds[i].sta.password, event->ap_cred[i].passphrase,
                               sizeof(event->ap_cred[i].passphrase));
                    }
                    /* If multiple AP credentials are received from WPS, connect with first one */
                    ESP_LOGI(TAG, "Connecting to SSID: %s, Passphrase: %s",
                             wps_ap_creds[0].sta.ssid, wps_ap_creds[0].sta.password);
                    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wps_ap_creds[0]) );
                }
                /*
                 * If only one AP credential is received from WPS, there will be no event data and
                 * esp_wifi_set_config() is already called by WPS modules for backward compatibility
                 * with legacy apps. So directly attempt connection here.
                 */
                ESP_ERROR_CHECK(esp_wifi_wps_disable());
                ESP_ERROR_CHECK(esp_wifi_connect());
            }
            break;
        }

        case WIFI_EVENT_STA_WPS_ER_FAILED:
        {
            ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_FAILED");
            ESP_ERROR_CHECK ( esp_wifi_wps_disable () );
            ESP_ERROR_CHECK ( esp_wifi_wps_enable ( &wps_config ) );
            ESP_ERROR_CHECK ( esp_wifi_wps_start ( 0 ) );
            break;
        }

        case WIFI_EVENT_STA_WPS_ER_TIMEOUT:
        {
            ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_TIMEOUT");
            ESP_ERROR_CHECK ( esp_wifi_wps_disable () );
            ESP_ERROR_CHECK ( esp_wifi_connect () );
            break;
        }

        case WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP:
        {
        	ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP");
        	break;
        }
    }
}

static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    switch ( event_id )
    {
		case IP_EVENT_STA_GOT_IP:
		{
			ESP_LOGI ( TAG, "IP_EVENT_STA_GOT_IP" );
			ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
			ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
			s_retry_num = 0;

			ws2812_led_set_hsv ( 110, 100, 25 );

			break;
		}

		case IP_EVENT_STA_LOST_IP:
		{
			ESP_LOGI ( TAG, "IP_EVENT_STA_LOST_IP" );

			break;
		}

		case IP_EVENT_AP_STAIPASSIGNED:
		{
			ESP_LOGI ( TAG, "IP_EVENT_AP_STAIPASSIGNED" );
			ip_event_ap_staipassigned_t * event = (ip_event_ap_staipassigned_t*) event_data;
			ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip));
			break;
		}
    }
}

static void wifi_init_apsta(void)
{
	ip_addr_t dnsserver;

    ESP_ERROR_CHECK ( esp_netif_init () );
    ESP_ERROR_CHECK ( esp_event_loop_create_default () );
    esp_netif_create_default_wifi_ap ();
    esp_netif_create_default_wifi_sta ();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK ( esp_wifi_init ( &cfg ) );

    esp_wifi_set_event_mask ( WIFI_EVENT_MASK_NONE );
    esp_wifi_set_bandwidth ( WIFI_MODE_APSTA, WIFI_BW_HT40 );

    ESP_ERROR_CHECK ( esp_event_handler_instance_register ( WIFI_EVENT,
    														ESP_EVENT_ANY_ID,
															&wifi_event_handler,
															NULL,
															NULL ) );

    ESP_ERROR_CHECK ( esp_event_handler_instance_register ( IP_EVENT,
    														ESP_EVENT_ANY_ID,
															&ip_event_handler,
															NULL,
															NULL));

    wifi_config_t wifi_config_ap = {
        .ap = {
            .ssid = SOFTAP_SSID,
            .ssid_len = strlen(SOFTAP_SSID),
            .channel = SOFTAP_CHANNEL,
            .password = SOFTAP_PASS,
            .max_connection = SOFTAP_MAX_STA_CONN,
            .authmode = WIFI_AUTH_OPEN
        },
    };

    ESP_ERROR_CHECK ( esp_wifi_set_mode ( WIFI_MODE_APSTA ) );
    ESP_ERROR_CHECK ( esp_wifi_set_config ( ESP_IF_WIFI_AP, &wifi_config_ap ) );

    /**/
    dhcps_offer_t dhcps_dns_value = OFFER_DNS;
    dhcps_set_option_info ( 6, &dhcps_dns_value, sizeof( dhcps_dns_value ) );

    /**/
    dnsserver.u_addr.ip4.addr = htonl ( MY_DNS_IP_ADDR );
    dnsserver.type = IPADDR_TYPE_V4;
    dhcps_dns_setserver ( &dnsserver );

    ESP_ERROR_CHECK ( esp_wifi_start () );
}

static void disconnect_task ( void * arg )
{
	wifi_sta_list_t sta = { 0 };

	for ( ;; )
	{
		esp_wifi_ap_get_sta_list ( &sta );

		for ( uint8_t i = 0; i < sta.num; i++ ) {
			ESP_LOGI ( TAG, "station "MACSTR", RSSI: %d",
					MAC2STR ( sta.sta [ i ].mac ),
					sta.sta [ i ].rssi );

			if ( sta.sta [ i ].rssi <= RSSI_THRESHOLD * 3 )
			{
				uint16_t aid = 0;
				esp_wifi_ap_get_sta_aid ( sta.sta [ i ].mac, &aid );
				esp_wifi_deauth_sta ( aid );
				ESP_LOGI ( TAG, "Good bye!" );
			}
		}

		vTaskDelay ( pdMS_TO_TICKS( 3000 ) );
	}
}

static void wps_task ( void * arg )
{
	uint32_t event_to_process;

	ESP_LOGI( TAG, "Configuring WPS button GPIO..." );
	gpio_config_t gpio_conf;
	gpio_conf.pin_bit_mask = 1ULL << GPIO_NUM_0;
	gpio_conf.mode = GPIO_MODE_INPUT;
	gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
	gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpio_conf.intr_type = GPIO_INTR_NEGEDGE;
	gpio_config( &gpio_conf );
	ESP_LOGI( TAG, "WPS button GPIO configured!" );

	gpio_install_isr_service ( 0 );
	gpio_isr_handler_add( GPIO_NUM_0, give_notification, ( void * ) wps_task_handle );

	for ( ;; )
	{
		event_to_process = ulTaskNotifyTake ( pdTRUE, portMAX_DELAY );

		if ( event_to_process )
		{
			ESP_LOGI ( TAG, "WPS started" );

			ESP_ERROR_CHECK ( esp_wifi_wps_disable () );
			ESP_ERROR_CHECK ( esp_wifi_wps_enable ( &wps_config ) );
			ESP_ERROR_CHECK ( esp_wifi_wps_start ( 0 ) );

			ws2812_led_set_hsv ( 12, 100, 25 );
		}
	}
}

static void IRAM_ATTR give_notification ( void * arg )
{
	TaskHandle_t * task_handle = ( TaskHandle_t * ) arg;
	BaseType_t higher_priority_task_woken = pdFALSE;

	vTaskNotifyGiveFromISR ( task_handle, &higher_priority_task_woken );

	portYIELD_FROM_ISR ( higher_priority_task_woken );
}

esp_err_t rgb_init ( void )
{
    esp_err_t err = ws2812_led_init();
    if (err != ESP_OK) {
        return err;
    }
    ws2812_led_set_hsv ( 1, 100, 25 );
    return ESP_OK;
}

/* end of file ---------------------------------------------------------------*/
