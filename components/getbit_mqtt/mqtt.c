/*
 * mqtt.c
 *
 * Created on: Sep 18, 2021
 * Author: Mauricio Barroso Benavides
 */

/* inclusions ----------------------------------------------------------------*/

#include "include/mqtt.h"

/* macros --------------------------------------------------------------------*/

/* typedef -------------------------------------------------------------------*/

/* internal data declaration -------------------------------------------------*/

static const char * TAG = "mqtt";

/* external data declaration -------------------------------------------------*/

/* internal functions declaration --------------------------------------------*/

static void mqttEventHandler(void * handler_args, esp_event_base_t base, int32_t event_id, void * event_data);

/* external functions definition ---------------------------------------------*/

esp_err_t mqtt_Init(mqtt_t * const me) {
	esp_err_t ret = ESP_OK;

	if(me->config.uri == NULL) {
		ESP_LOGI(TAG, "No URI");

		return ESP_FAIL;
	}

	if(me->config.client_cert_pem == NULL) {
		ESP_LOGI(TAG, "No client certificate");

		return ESP_FAIL;
	}

	if(me->config.client_key_pem == NULL) {
		ESP_LOGI(TAG, "No private key");

		return ESP_FAIL;
	}

	if(me->config.cert_pem == NULL) {
		ESP_LOGI(TAG, "No server certificate");

		return ESP_FAIL;
	}

#ifdef CONFIG_BITEC_MQTT_LWT_ENABLE
	me->config.lwt_topic = CONFIG_BITEC_MQTT_LWT_TOPIC;
	me->config.lwt_msg = CONFIG_BITEC_MQTT_LWT_MESSAGE;
	me->config.lwt_msg_len = CONFIG_BITEC_MQTT_LWT_LENGHT;
	me->config.lwt_qos = CONFIG_BITEC_MQTT_LWT_QOS;
#endif

	me->client = esp_mqtt_client_init(&me->config);

	if(me->mqttEventHandler == NULL) {
		ret = esp_mqtt_client_register_event(me->client, MQTT_EVENT_ANY, mqttEventHandler, NULL);
	}
	else {
		ret = esp_mqtt_client_register_event(me->client, MQTT_EVENT_ANY, me->mqttEventHandler, NULL);
	}

	return ret;
}

/* internal functions definition ---------------------------------------------*/

static void mqttEventHandler(void * handler_args, esp_event_base_t base, int32_t event_id, void * event_data) {
	/* Keep empty */
}

/* end of file ---------------------------------------------------------------*/
