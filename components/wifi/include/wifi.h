/**
  ******************************************************************************
  * @file           : wifi.h
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef WIFI_H_
#define WIFI_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_err.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"

/* Exported define -----------------------------------------------------------*/

/* Exported macro ------------------------------------------------------------*/

/* Exported typedef ----------------------------------------------------------*/
typedef void (* wifi_event_handler_t)(void *, esp_event_base_t, int32_t, void *);
typedef void (* ip_event_handler_t)(void *, esp_event_base_t, int32_t, void *);
typedef void (* prov_event_handler_t)(void *, esp_event_base_t, int32_t, void *);

typedef struct {
	wifi_event_handler_t wifi_event_handler;
	ip_event_handler_t ip_event_handler;
	prov_event_handler_t prov_event_handler;
	void * wifi_event_data;
	void * ip_event_data;
	void * prov_event_data;
} wifi_t; /* todo: write descriptions */

/* Exported variables --------------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/
esp_err_t wifi_init(wifi_t * const me);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_H_ */

/***************************** END OF FILE ************************************/
