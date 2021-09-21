/*
 * mqtt.h
 *
 * Created on: Sep 18, 2021
 * Author: Mauricio Barroso Benavides
 */

#ifndef _MQTT_H_
#define _MQTT_H_

/* inclusions ----------------------------------------------------------------*/

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_err.h"
#include "esp_log.h"

#include "mqtt_client.h"

/* cplusplus -----------------------------------------------------------------*/

#ifdef __cplusplus
extern "C" {
#endif

/* macros --------------------------------------------------------------------*/

/* typedef -------------------------------------------------------------------*/

typedef void (* mqttEventHandler_t)(void *, esp_event_base_t, int32_t, void *);

typedef struct {
	esp_mqtt_client_config_t config;
	esp_mqtt_client_handle_t client;
	mqttEventHandler_t mqttEventHandler;
} mqtt_t;

/* external data declaration -------------------------------------------------*/

/* external functions declaration --------------------------------------------*/

esp_err_t mqtt_Init(mqtt_t * const me);

/* cplusplus -----------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

/** @} doxygen end group definition */

/* end of file ---------------------------------------------------------------*/

#endif /* #ifndef _MQTT_H_ */
