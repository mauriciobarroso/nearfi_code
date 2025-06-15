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

#include "esp_err.h"
#include "esp_system.h"
#include "esp_wifi_types_generic.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_mac.h"

#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include <netdb.h>
#include "driver/i2c_master.h"
#include "portmacro.h"
#include "sdkconfig.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"
#include "lwip/ip4_addr.h"
#include "lwip/lwip_napt.h"

#include "led.h"
#include "button.h"
#include "buzzer.h"
#include "at24cs0x.h"
#include "tpl5010.h"

#include "misc.c"
#include "nvs.c"
#include "server.c"
#include "typedefs.h"

/* Macros --------------------------------------------------------------------*/
/* Peripherals GPIOs macros */
#define I2C_BUS_SDA_PIN		CONFIG_PERIPHERALS_I2C_SDA_PIN
#define I2C_BUS_SCL_PIN		CONFIG_PERIPHERALS_I2C_SCL_PIN
#define TPL5010_WAKE_PIN	CONFIG_PERIPHERALS_EWDT_WAKE_PIN
#define TPL5010_DONE_PIN	CONFIG_PERIPHERALS_EWDT_DONE_PIN
#define BUTTON_PIN			CONFIG_PERIPHERALS_BUTTON_PIN
#define BUZZER_PIN			CONFIG_PERIPHERALS_BUZZER_PIN
#define LED_PIN				CONFIG_PERIPHERALS_LEDS_PIN

/* Settings macros */
#define SETTINGS_EEPROM_ADDR		0x0
#define SETTINGS_SSID_DEFAULT		"NearFi"
#define SETTINGS_CLIENTS_DEFAULT	15
#define SETTINGS_TIME_DEFAULT		60000

/* SPIFFS macros */
#define SPIFFS_BASE_PATH	"/spiffs"

/**/
#define APP_QUEUE_LEN_DEFAULT	5

/**/
#define APP_TASK_HEALTH_MONITOR_PRIORITY	tskIDLE_PRIORITY + 1
#define APP_TASK_ACTIONS_PRIORITY			tskIDLE_PRIORITY + 2
#define APP_TASK_ALERTS_PRIORITY			tskIDLE_PRIORITY + 3	
#define APP_TASK_NETWORK_PRIORITY			tskIDLE_PRIORITY + 4
#define APP_TASK_CLIENTS_PRIORITY			tskIDLE_PRIORITY + 5
#define APP_TASK_TICK_PRIORITY				tskIDLE_PRIORITY + 6
#define APP_TASK_RESPONSES_MANAGER_PRIORITY	tskIDLE_PRIORITY + 7
#define APP_TASK_SYSTEM_EVENTS_PRIORITY		tskIDLE_PRIORITY + 8

/* Typedef -------------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
/* Tag for debug */
static const char *TAG = "NearFi";

static uint8_t serial_number[AT24CS0X_SN_SIZE];
static uint8_t mac_addr[6];
static settings_t settings;
static client_list_t clients;
static uint32_t otp = 0;

/* Components */
static button_t button;
static led_t led;
static buzzer_t buzzer;
static tpl5010_t wdt;
static at24cs0x_t eeprom;
static i2c_master_bus_handle_t i2c_bus_handle;

/* OTA variables */
#ifdef CONFIG_OTA_ENABLE
extern const uint8_t ota_cert[] asm("_binary_server_pem_start") ;
static char *ota_url = CONFIG_OTA_FILE_URL;
#endif /* CONFIG_OTA_ENABLE */

/* Buzzer sounds */
sound_t sound_beep[] = {
    {880, 100, 100},  // La5
};

sound_t sound_warning[] = {
    {784, 150, 100},   // Sol5
    {659, 150, 100},   // Mi5
    {784, 150, 100},   // Sol5
};
sound_t sound_success[] = {
    {784, 120, 100},   // Sol5
    {988, 180, 100},   // Si5
    {1175, 220, 80},   // Re6
};

sound_t sound_fail[] = {
    {880, 200, 100},   // La5
    {698, 180, 100},   // Fa5
	{523, 250, 100},   // Do5	
};

sound_t sound_startup[] = {
    { 1000,  80,  90 },   // suave, base
    { 1500, 100, 100 },   // subida progresiva
    { 2000, 120, 100 },   // nota limpia, aguda
    { 1500,  60,  80 },   // leve caída
    { 1800, 100, 90  },   // final brillante
};

/**/
static QueueHandle_t system_events_queue;
static QueueHandle_t clients_requests_queue;
static QueueHandle_t actions_requests_queue;
static QueueHandle_t network_requests_queue;
static QueueHandle_t alerts_requests_queue;
static QueueHandle_t processes_responses_queue;


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
static void print_dev_info(void);

/* RTOS tasks */
static void tick_task(void *arg);
static void health_monitor_task(void *arg);

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
static void list_add(uint8_t *mac, uint8_t aid);
static void list_remove(uint8_t *mac);

static esp_err_t spiffs_init(const char *base_path);

/**/
static esp_err_t app_create_queues(void);
static esp_err_t app_create_tasks(void);
static void system_events_manager_task(void *arg);
static void processes_responses_manager_task(void *arg);
static void alerts_task(void *arg);
static void network_task(void *arg);
static void actions_task(void *arg);
static void clients_task(void *arg);

static void button_cb(void *arg);

static void event_send_to_manager(event_t *event);
static void event_send_response(event_t *event, event_response_t response);
static void event_request_to_alerts(event_t *const event, event_request_t request);
static void event_request_to_actions(event_t *const event, event_request_t request);
static void event_request_to_network(event_t *const event, event_request_t request);
static void event_set_alerts(event_t *const event, uint8_t r, uint8_t g, uint8_t b, uint16_t on_time, uint16_t off_time);
static void event_set_data_client(event_t *const me, uint8_t aid, uint8_t *mac);

static int tls_health_check(void);
/* Main ----------------------------------------------------------------------*/
void app_main(void) {
	/**/
	ESP_ERROR_CHECK(app_create_queues());
	ESP_ERROR_CHECK(app_create_tasks());

	/* Initialize a LED instance */
	ESP_ERROR_CHECK(led_strip_init(
			&led,
			LED_PIN,
			2));

	led_rgb_set_continuous(&led, 127, 0, 127);
	
	/* Initialize a buzzer instance */
	buzzer_init(
			&buzzer,
			BUZZER_PIN,
			LEDC_TIMER_0,
			LEDC_CHANNEL_0);
			
	buzzer_run(&buzzer, sound_startup, 5);
			
	/* Initialize a button instance */
	ESP_ERROR_CHECK(button_init(
			&button,
			BUTTON_PIN,
			BUTTON_EDGE_FALLING,
			tskIDLE_PRIORITY + 4,
			configMINIMAL_STACK_SIZE * 2));
			
	/* Add butttons callbacks functions for single, medium and log click */
	button_add_cb(&button, BUTTON_CLICK_SINGLE, button_cb, (void *)EVENT_REQUEST_RESET);
	button_add_cb(&button, BUTTON_CLICK_MEDIUM, button_cb, (void *)EVENT_REQUEST_OTA);
	button_add_cb(&button, BUTTON_CLICK_LONG, button_cb, (void *)EVENT_REQUEST_RESTORE);

	/* Initialize TPL5010 instance */
	ESP_ERROR_CHECK(tpl5010_init(
			&wdt,
			TPL5010_WAKE_PIN,
			TPL5010_DONE_PIN));


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
			&eeprom, /* AT24CS0X instance */
			i2c_bus_handle, /* I2C bus instance */
			AT24CS0X_I2C_ADDRESS, /* I2C device address */
			AT24CS0X_MODEL_02)); /* I2C custom write function */

	/* Load device settings */
	settings_load();

	/* Initialize NVS */
	ESP_ERROR_CHECK(nvs_init());

	/* Initialize Wi-Fi */
	ESP_ERROR_CHECK(wifi_init());
	
	/* Initialize clients list */
	list_init();

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
		ESP_LOGI (TAG, "NAT is enabled");
		
		/* Connect to router */
		esp_wifi_connect();		
	}
	else {
		ESP_LOGI(TAG, "Not provisioned. Waiting while the process running...");

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
	}
}

/* Private function definition -----------------------------------------------*/
/* Initialization functions */
static esp_err_t wifi_init(void)
{
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
	dns_info.ip.u_addr.ip4.addr = ipaddr_addr("8.8.8.8");
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
	
	/* Return ESP_OK */
	return ret;
}

/* Event handlers */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
		int32_t event_id, void *event_data)
{
	event_t event;
	event.src = EVENT_PROCESS_WIFI;
		
	switch (event_id) {
	case WIFI_EVENT_STA_DISCONNECTED: {
		ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED");
		
		event.dst = EVENT_PROCESS_NETWORK;
		event.request = EVENT_REQUEST_DISCONNECTED;
		event_send_to_manager(&event);	
		
		break;
	}

	case WIFI_EVENT_AP_STACONNECTED: {
		ESP_LOGI(TAG, "WIFI_EVENT_AP_STACONNECTED");
		
		event.dst = EVENT_PROCESS_CLIENTS;
		event.request = EVENT_REQUEST_ADD;
		event.data.client.aid = ((wifi_event_ap_staconnected_t *)event_data)->aid;
		
		for (uint8_t i = 0; i < 6; i++) {
			event.data.client.mac[i] = ((wifi_event_ap_staconnected_t *)event_data)->mac[i];
		}
		
		event_send_to_manager(&event);
		
		break;
	}

	case WIFI_EVENT_AP_STADISCONNECTED: {
		ESP_LOGI(TAG, "WIFI_EVENT_AP_STADISCONNECTED");
		
		event.dst = EVENT_PROCESS_CLIENTS;
		event.request = EVENT_REQUEST_REMOVE;
		event.data.client.aid = ((wifi_event_ap_staconnected_t *)event_data)->aid;
		
		for (uint8_t i = 0; i < 6; i++) {
			event.data.client.mac[i] = ((wifi_event_ap_staconnected_t *)event_data)->mac[i];
		}
		
		event_send_to_manager(&event);
		
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
	event_t event;
	event.src = EVENT_PROCESS_IP;
	
	switch (event_id) {
	case IP_EVENT_STA_GOT_IP: {
		ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP");	
		
		event.dst = EVENT_PROCESS_NETWORK;
		event.request = EVENT_REQUEST_CONNECTED;
		event_send_to_manager(&event);
				
		break;
	}

	default:
	ESP_LOGI(TAG, "Other IP event");
	break;
	}
}

static void prov_event_handler(void *arg, esp_event_base_t event_base,
		int32_t event_id, void *event_data)
{
	event_t event;
	event.src = EVENT_PROCESS_PROV;
	
	switch (event_id) {
	case WIFI_PROV_START:{
		ESP_LOGI(TAG, "WIFI_PROV_START");
		event_set_alerts(&event, 0, 0, 255, 2000, 1000);
		event.request = EVENT_REQUEST_FADE;
		event_send_to_manager(&event);
				
		break;
	}
	
	
	case WIFI_PROV_END: {
		ESP_LOGI(TAG, "WIFI_PROV_END");
		
		event.dst = EVENT_PROCESS_ACTIONS;
		event.request = EVENT_REQUEST_RESET;
		event_send_to_manager(&event);
		
		break;
	}

	case WIFI_PROV_CRED_FAIL: {
		ESP_LOGI(TAG, "WIFI_PROV_CRED_FAIL");
		
		event.dst = EVENT_PROCESS_ACTIONS;
		event.request = EVENT_REQUEST_RESTORE;
		event_send_to_manager(&event);
		
		break;
	}

	default: {
		ESP_LOGI(TAG, "Other event");
		break;
	}
	}
}


/* Provisioning utils */
static char *get_device_service_name(const char *ssid_prefix)
{
	char *name = NULL;

	name = malloc((strlen(ssid_prefix) + 6 + 1) * sizeof(*name));
	sprintf(name, "%s%02X%02X%02X", ssid_prefix, mac_addr[3], mac_addr[4], mac_addr[5]);

	return name;
}

static esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf,
		ssize_t inlen, uint8_t **outbuf, ssize_t * outlen, void *priv_data)
		{
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
static void tick_task(void *arg) {
	TickType_t last_wake = xTaskGetTickCount();
	event_t event;
	event.src = EVENT_PROCESS_TICK;

	for (;;) {
		vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
		
		/* Send TICK request to CLIENTS process evert second */
		event.dst = EVENT_PROCESS_CLIENTS;
		event.request = EVENT_REQUEST_TICK;
		event_send_to_manager(&event);
	}
}

static void health_monitor_task(void *arg) {
    TickType_t last_wake = xTaskGetTickCount();
    event_t event;
    event.src = EVENT_PROCESS_HEALTH;

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10000));
        printf("health monitor\n");
        tls_health_check();
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

static void print_dev_info(void)
{
	char *ap_prov_name = get_device_service_name(CONFIG_WIFI_PROV_SSID_PREFIX);

	at24cs0x_read_serial_number(&eeprom, serial_number);

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
	if (at24cs0x_read(&eeprom, SETTINGS_EEPROM_ADDR, (uint8_t*)&settings,
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
	if (at24cs0x_write(&eeprom, SETTINGS_EEPROM_ADDR, (uint8_t*)&settings,
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

static void list_add(uint8_t *mac, uint8_t aid)
{
	/* Reallocate memory for the new client */
	clients.client = (client_t *)realloc(clients.client, (clients.num + 1) * sizeof(client_t));

	if (clients.client == NULL) {
		ESP_LOGE(TAG, "Failed to add new client to list");
		return;
	}

	/* Fill the new client data */
	strncpy((char *)clients.client[clients.num].mac, (char *)mac, 6);
	clients.client[clients.num].time = settings_get_time();
	clients.client[clients.num].aid = aid;

	/* Increase the clients number */
	clients.num++;
}

static void list_remove(uint8_t *mac)
{
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
			.max_files = 5,
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

static esp_err_t app_create_queues(void) {
	ESP_LOGI(TAG, "Creatbg app queues...");
	
	system_events_queue = xQueueCreate(APP_QUEUE_LEN_DEFAULT * 2, sizeof(event_t));
	
	if (system_events_queue == NULL) {
		return ESP_FAIL;
	}
	
	processes_responses_queue = xQueueCreate(APP_QUEUE_LEN_DEFAULT * 2, sizeof(event_t));
	
	if (processes_responses_queue == NULL) {
		return ESP_FAIL;
	}
	
	clients_requests_queue = xQueueCreate(APP_QUEUE_LEN_DEFAULT, sizeof(event_t));
	
	if (clients_requests_queue == NULL) {
		return ESP_FAIL;
	}
	
	actions_requests_queue = xQueueCreate(APP_QUEUE_LEN_DEFAULT, sizeof(event_t));
	
	if (actions_requests_queue == NULL) {
		return ESP_FAIL;
	}
	
	alerts_requests_queue = xQueueCreate(APP_QUEUE_LEN_DEFAULT, sizeof(event_t));
	
	if (alerts_requests_queue == NULL) {
		return ESP_FAIL;
	}
	
	network_requests_queue = xQueueCreate(APP_QUEUE_LEN_DEFAULT, sizeof(event_t));
	
	if (network_requests_queue == NULL) {
		return ESP_FAIL;
	}	
	
	return ESP_OK;
}

static esp_err_t app_create_tasks(void) {
	BaseType_t status;	
		
	/**/
	status = xTaskCreatePinnedToCore(
		tick_task, 
		"Tick Task",
		configMINIMAL_STACK_SIZE * 2,
		NULL,
		APP_TASK_TICK_PRIORITY,
		NULL,
		1);
		
	if (status != pdPASS) {
		return ESP_FAIL;
	}
		
	status = xTaskCreatePinnedToCore(
		health_monitor_task, 
		"Health Monitor Task",
		configMINIMAL_STACK_SIZE * 2,
		NULL,
		APP_TASK_HEALTH_MONITOR_PRIORITY,
		NULL,
		0);
		
	if (status != pdPASS) {
		return ESP_FAIL;
	}
	
	status = xTaskCreatePinnedToCore(
		system_events_manager_task, 
		"Systems Events Manager Task",
		configMINIMAL_STACK_SIZE,
		NULL,
		APP_TASK_SYSTEM_EVENTS_PRIORITY,
		NULL,
		1);
		
	if (status != pdPASS) {
		return ESP_FAIL;
	}
	
	status = xTaskCreatePinnedToCore(
		processes_responses_manager_task, 
		"Processes Responses Manager Task",
		configMINIMAL_STACK_SIZE,
		NULL,
		APP_TASK_RESPONSES_MANAGER_PRIORITY,
		NULL,
		1);
		
	if (status != pdPASS) {
		return ESP_FAIL;
	}
	
	status = xTaskCreatePinnedToCore(
		alerts_task, 
		"Alerts Task",
		configMINIMAL_STACK_SIZE * 2,
		NULL,
		APP_TASK_ALERTS_PRIORITY,
		NULL,
		1);
		
	if (status != pdPASS) {
		return ESP_FAIL;
	}
	
	status = xTaskCreatePinnedToCore(
		network_task, 
		"Network Task",
		configMINIMAL_STACK_SIZE * 4,
		NULL,
		APP_TASK_NETWORK_PRIORITY,
		NULL,
		0);
		
	if (status != pdPASS) {
		return ESP_FAIL;
	}
	
	status = xTaskCreatePinnedToCore(
		actions_task, 
		"Actions Task",
		configMINIMAL_STACK_SIZE * 2,
		NULL,
		APP_TASK_ACTIONS_PRIORITY,
		NULL,
		1);
		
	if (status != pdPASS) {
		return ESP_FAIL;
	}
	
	status = xTaskCreatePinnedToCore(
		clients_task, 
		"Clients Task",
		configMINIMAL_STACK_SIZE * 2,
		NULL,
		APP_TASK_CLIENTS_PRIORITY,
		NULL,
		1);
		
	if (status != pdPASS) {
		return ESP_FAIL;
	}
	
	return ESP_OK;
}

static void system_events_manager_task(void *arg) {
	BaseType_t status;
	event_t event;
	uint32_t timestamp = 0;
	
	for (;;) {
		status = xQueueReceive(system_events_queue, &event, portMAX_DELAY);
		
		if (status == pdPASS) {
			event.timestamp = timestamp;			
			switch (event.dst) {
				case EVENT_PROCESS_CLIENTS:
				xQueueSend(clients_requests_queue, &event, 0);
				break;
				
				case EVENT_PROCESS_NETWORK:		
				/* Send event to Network  */
				xQueueSend(network_requests_queue, &event, 0);
				
				if (event.request == EVENT_REQUEST_OTA) {
					event_set_alerts(&event, 128, 128, 0, 1000, 1000);
					event_request_to_alerts(&event, EVENT_REQUEST_FADE);			
				}
				
				else if (event.request == EVENT_REQUEST_CONNECTED) {
					event_set_alerts(&event, 0, 255, 0, 0, 0);
					event_request_to_alerts(&event, EVENT_REQUEST_SET);			
				}
				
				else if (event.request == EVENT_REQUEST_DISCONNECTED) {
					event_set_alerts(&event, 255, 0, 0, 200, 200);
					event_request_to_alerts(&event, EVENT_REQUEST_FADE);			
				}
				break;
				
				case EVENT_PROCESS_ALERTS:
				xQueueSend(alerts_requests_queue, &event, 0);
				break;
				
				case EVENT_PROCESS_ACTIONS:
				xQueueSend(actions_requests_queue, &event, 0);
				break;
				
				default:
				break;
			}
			
			if (event.src == EVENT_PROCESS_TICK) {
				timestamp++;
			}
		}
	}
}

static void processes_responses_manager_task(void *arg) {
	BaseType_t status;
	event_t event;
	
	for (;;) {
		status = xQueueReceive(processes_responses_queue, &event, portMAX_DELAY);
		
		if (status == pdPASS) {
			switch (event.response) {
				case EVENT_RESPONSE_FULL:
				if (event.src == EVENT_PROCESS_CLIENTS) {
					event_set_alerts(&event, 180, 75, 0, 0, 0);
					event_request_to_alerts(&event, EVENT_REQUEST_SET);					
				}
				break;
				
				case EVENT_RESPONSE_AVAILABLE:
				if (event.src == EVENT_PROCESS_CLIENTS) {
					event_set_alerts(&event, 0, 255, 0, 0, 0);
					event_request_to_alerts(&event, EVENT_REQUEST_SET);
				}
				break;
				
				case EVENT_RESPONSE_FAIL:
				if (event.src == EVENT_PROCESS_NETWORK && event.request == EVENT_REQUEST_OTA) {
					event_set_alerts(&event, 0, 255, 0, 0, 0);
					event_request_to_alerts(&event, EVENT_REQUEST_SET);
				}
				else if (event.src == EVENT_PROCESS_NETWORK && event.request == EVENT_REQUEST_DISCONNECTED) {
					event_request_to_actions(&event, EVENT_REQUEST_RESET);
				}
				else if (event.src == EVENT_PROCESS_CLIENTS && event.request == EVENT_REQUEST_ADD) {
					event_request_to_network(&event, EVENT_REQUEST_DEAUTH);
				}
				break;
				
				case EVENT_RESPONSE_SUCCESS:
				if (event.src == EVENT_PROCESS_CLIENTS && event.request == EVENT_REQUEST_ADD) {
					event.data.led.update = false;
					event_request_to_alerts(&event, EVENT_REQUEST_SUCESS);
				}
				else if (event.src == EVENT_PROCESS_ALERTS) {
					event.data.led.update = false;
					event_request_to_alerts(&event, EVENT_REQUEST_SET);
				}	
				else if (event.src == EVENT_PROCESS_NETWORK && event.request == EVENT_REQUEST_OTA) {
					event_request_to_actions(&event, EVENT_REQUEST_RESET);
				}
				break;
				
				case EVENT_RESPONSE_TIMEOUT:
				if (event.src == EVENT_PROCESS_CLIENTS && event.request == EVENT_REQUEST_TICK) {
					event_request_to_network(&event, EVENT_REQUEST_DEAUTH);
				}
				break;
				
				default:
				break;
			}
		}
	}
}

static void alerts_task(void *arg) {
	BaseType_t status;
	event_t event;
	uint8_t r = 0, g = 0, b = 0;
	uint16_t on_time = 0, off_time = 0;
	
	for (;;) {		
		status = xQueueReceive(alerts_requests_queue, &event, portMAX_DELAY);
		
		if (status == pdPASS) {
			event.src = EVENT_PROCESS_ALERTS;
			 
			switch (event.request) {
				case EVENT_REQUEST_SET:
				ESP_LOGW(TAG, "ALERTS_SET");
				
				if (event.data.led.update) {
					r = event.data.led.r;
					g = event.data.led.g;
					b = event.data.led.b;	
				}				
					
				led_rgb_set_continuous(&led, r, g, b);
				break;
				
				case EVENT_REQUEST_CLEAR:
				ESP_LOGW(TAG, "ALERTS_CLEAR");	
				led_rgb_set_continuous(&led, 0, 0, 0);
				break;
				
				case EVENT_REQUEST_FADE:
				ESP_LOGW(TAG, "ALERTS_FADE");

				if (event.data.led.update) {
					r = event.data.led.r;
					g = event.data.led.g;
					b = event.data.led.b;			
					on_time = event.data.led.on_time;
					off_time = event.data.led.off_time;
				}
				
				led_rgb_set_fade(&led, r, g, b, on_time, off_time);
				break;
				
				case EVENT_REQUEST_SUCESS:
				ESP_LOGW(TAG, "ALERTS_SUCCESS");
				led_rgb_set_continuous(&led, 100, 100, 100);
				buzzer_run(&buzzer, sound_success, 3);
				vTaskDelay(pdMS_TO_TICKS(200));
				event_send_response(&event, EVENT_RESPONSE_SUCCESS);			
				break;
				
				case EVENT_REQUEST_FAIL:
				ESP_LOGW(TAG, "ALERTS_FAIL");
				led_rgb_set_continuous(&led, 255, 0, 0);
				buzzer_run(&buzzer, sound_fail, 3);
				vTaskDelay(pdMS_TO_TICKS(200));
				event_send_response(&event, EVENT_RESPONSE_SUCCESS);
				break;			
				
				default:
				break;
			}
		}		
	}	
}

static void network_task(void *arg) {
	BaseType_t status;
	event_t event;
	uint8_t reconnect_try = 0;
			
	for (;;) {
		status = xQueueReceive(network_requests_queue, &event, portMAX_DELAY);
		
		if (status == pdPASS) {
			event.src = EVENT_PROCESS_NETWORK;
			 
			switch (event.request) {
				case EVENT_REQUEST_OTA:
				ESP_LOGW(TAG, "NETWORK_OTA");
				if (ota_update(ota_url, (char*)ota_cert, 120000) == ESP_OK) {
					event_send_response(&event, EVENT_RESPONSE_SUCCESS);
				}
				else {
					event_send_response(&event, EVENT_RESPONSE_FAIL);
				}
				break;
				
				case EVENT_REQUEST_DISCONNECTED:
				ESP_LOGW(TAG, "NETWORK_DISCONNECTED");		
				esp_wifi_disconnect();
				esp_wifi_connect();
				
				if (reconnect_try++ >= 20) {
					event_send_response(&event, EVENT_RESPONSE_FAIL);
				}
				break;
				
				case EVENT_REQUEST_DEAUTH:
				ESP_LOGW(TAG, "NETWORK_DEAUTH");
				ESP_LOGE(TAG, MACSTR " DEAUTH", MAC2STR(event.data.client.mac));
				esp_wifi_deauth_sta(event.data.client.aid);
				break;
				
				case EVENT_REQUEST_CONNECTED:
				ESP_LOGW(TAG, "NETWORK_CONNECTED");					
				break;
				
				case EVENT_REQUEST_RESET:
				ESP_LOGW(TAG, "NETWORK_RESET");
				esp_wifi_stop();
				vTaskDelay(pdMS_TO_TICKS(100));
				esp_wifi_start();
				vTaskDelay(pdMS_TO_TICKS(100));
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
		
	for (;;) {
		status = xQueueReceive(actions_requests_queue, &event, portMAX_DELAY);
		
		if (status == pdPASS) {
			 
			event.src = EVENT_PROCESS_ACTIONS;
			switch (event.request) {
				case EVENT_REQUEST_RESET:
				ESP_LOGW(TAG, "ACTIONS_RESET");
				/* todo: send response and delay to notify with sound */
				esp_restart();
				break;
				
				case EVENT_REQUEST_RESTORE:
				ESP_LOGW(TAG, "ACTIONS_RESTORE");
				erase_wifi_creds(NULL);
				break;
				
				default:
				break;
			}
		}
	}
}

static void clients_task(void *arg)	{
	BaseType_t status;
	event_t event;
	wifi_sta_list_t sta_list;
		
	for (;;) {
		status = xQueueReceive(clients_requests_queue, &event, portMAX_DELAY);
		
			if (status == pdPASS) {
			 
			event.src = EVENT_PROCESS_CLIENTS;
			switch (event.request) {
				case EVENT_REQUEST_ADD:
				ESP_LOGW(TAG, "CLIENTS_ADD");
				
				esp_wifi_ap_get_sta_list(&sta_list);
				
				for (uint8_t i = 0; i < sta_list.num; i++) {
					if (!strncmp((char *)sta_list.sta[i].mac, (char *)event.data.client.mac, 6)) {
						if (sta_list.sta[i].rssi <= CONFIG_APP_RSSI_THRESHOLD_JOIN) {
							event_send_response(&event, EVENT_RESPONSE_FAIL);
						}
						else {
							list_add(event.data.client.mac, event.data.client.aid);
							ESP_LOGI(TAG, MACSTR " added to list. Clients in list: %d/%d", MAC2STR(event.data.client.mac), clients.num, settings_get_clients());
							event_send_response(&event, EVENT_RESPONSE_SUCCESS);
							
							if (clients.num == settings_get_clients()) {
								event_send_response(&event, EVENT_RESPONSE_FULL);
							}
						}
					}
				}						
				
				break;
				
				case EVENT_REQUEST_REMOVE:
				ESP_LOGW(TAG, "CLIENTS_REMOVE");
				
				/* Process */
				list_remove(event.data.client.mac);
				ESP_LOGE(TAG, MACSTR " removed from list. Clients in list: %d/%d", MAC2STR(event.data.client.mac), clients.num, settings_get_clients());
				
				if (clients.num == 0) {
					event_send_response(&event, EVENT_RESPONSE_EMPTY);
				}
				else {
					event_send_response(&event, EVENT_RESPONSE_AVAILABLE);
				}
				
				break;
				
				case EVENT_REQUEST_TICK:
//				ESP_LOGW(TAG, "CLIENTS_TICK");
				
				/* Process */
				for (uint8_t i = 0; i < clients.num; i++) {
					if (--clients.client[i].time == 0) {
						event.data.client.aid = clients.client[i].aid;
						strncpy((char *)event.data.client.mac, (char *)clients.client[i].mac, 6);
						event_send_response(&event, EVENT_RESPONSE_TIMEOUT);
					}
				}
				
				break;
				
				default:
				break;
			}
		}
	}
}

void button_cb(void *arg) {
	event_t event;
	event.src = EVENT_PROCESS_BUTTON;
	event.request = (event_request_t)arg;
	
	if (event.request == EVENT_REQUEST_OTA) {
		event.dst = EVENT_PROCESS_NETWORK;
	}
	else {
		event.dst = EVENT_PROCESS_ACTIONS;
	}
	
	event_send_to_manager(&event);
}

static void event_send_to_manager(event_t *event)
{
	switch (event->src) {
		case EVENT_PROCESS_WIFI:
		case EVENT_PROCESS_IP:
		case EVENT_PROCESS_PROV:
		case EVENT_PROCESS_TICK:
		case EVENT_PROCESS_BUTTON:
		case EVENT_PROCESS_WDT:
		xQueueSend(system_events_queue, event, 0);
		break;
 		
		default:
		ESP_LOGW(TAG, "Unknown process: %d", event->src);
		break;
	}
}

static void event_send_response(event_t *event, event_response_t response)
{
	event->response = response;
	xQueueSend(processes_responses_queue, event, 0);
}

static void event_request_to_alerts(event_t *const event, event_request_t request)
{
	event->dst = EVENT_PROCESS_ALERTS;
	event->request = request;
	xQueueSend(alerts_requests_queue, event, 0);
}

static void event_request_to_actions(event_t *const event, event_request_t request)
{
	event->dst = EVENT_PROCESS_ACTIONS;
	event->request = request;
	xQueueSend(actions_requests_queue, event, 0);
}

static void event_request_to_network(event_t *const event, event_request_t request)
{
	event->dst = EVENT_PROCESS_NETWORK;
	event->request = request;
	xQueueSend(network_requests_queue, event, 0);
}

static void event_set_alerts(event_t *const event, uint8_t r, uint8_t g, uint8_t b, uint16_t on_time, uint16_t off_time)
{
	event->data.led.update = true;
	event->data.led.r = r;
	event->data.led.g = g;
	event->data.led.b = b;
	event->data.led.on_time = on_time;
	event->data.led.off_time = off_time;
}

static int tls_health_check(void)
{
    const char *host = "google.com";
    const char *port = "443";
    struct addrinfo hints = {
        .ai_family   = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res = NULL;
    int ret = -1, sock;

    if (getaddrinfo(host, port, &hints, &res) != 0 || res == NULL) {
        goto cleanup;  // no res o falló resolución
    }

    sock = socket(res->ai_family, res->ai_socktype, 0);
    if (sock < 0) {
        goto cleanup;  // no olvides liberar res
    }

    struct timeval timeout = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    if (connect(sock, res->ai_addr, res->ai_addrlen) == 0) {
        ret = 0;       // OK
    }

    // Cerrá siempre el socket
    close(sock);

cleanup:
    if (res) {
        freeaddrinfo(res);
    }
    return ret;
}


/***************************** END OF FILE ************************************/

