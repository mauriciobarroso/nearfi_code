/**
  ******************************************************************************
  * @file           : typedefs.h
  * @author         : Mauricio Barroso Benavides
  * @date           : May 27, 2024
  * @brief          : todo: write brief 
  ******************************************************************************
  * @attention
  *
  * MIT License
  *
  * Copyright (c) 2024 Mauricio Barroso Benavides
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
#ifndef TYPEDEFS_H_
#define TYPEDEFS_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include "lwip/stats.h"

/* Exported Macros -----------------------------------------------------------*/


/* Exported typedef ----------------------------------------------------------*/
typedef enum {
	EVENT_PROCESS_NO = 0,
	
	/* Producers */
	EVENT_PROCESS_WIFI,
	EVENT_PROCESS_IP,
	EVENT_PROCESS_PROV,
	EVENT_PROCESS_TICK,
	EVENT_PROCESS_BUTTON,
	EVENT_PROCESS_WDT,
	EVENT_PROCESS_HEALTH,
	
	/* Consumers */
	EVENT_PROCESS_ACTIONS,
	EVENT_PROCESS_ALERTS,
	EVENT_PROCESS_NETWORK,
	EVENT_PROCESS_CLIENTS,
	EVENT_PROCESS_MAX
} event_process_t;

typedef enum {
	EVENT_REQUEST_NO = 0,
	EVENT_REQUEST_OTA,
	EVENT_REQUEST_FADE,
	EVENT_REQUEST_SET,
	EVENT_REQUEST_CLEAR,
	EVENT_REQUEST_SUCESS,
	EVENT_REQUEST_FAIL,
	EVENT_REQUEST_WARNING,
	EVENT_REQUEST_CONNECTED,
	EVENT_REQUEST_DISCONNECTED,
	EVENT_REQUEST_ENABLE,
	EVENT_REQUEST_DISABLE,
	EVENT_REQUEST_ADD,
	EVENT_REQUEST_REMOVE,
	EVENT_REQUEST_RESET,
	EVENT_REQUEST_RESTORE,
	EVENT_REQUEST_WDT,
	EVENT_REQUEST_TICK,
	EVENT_REQUEST_DEAUTH,
	EVENT_REQUEST_MAX
} event_request_t;

typedef enum {
	EVENT_RESPONSE_NO = 0,
	EVENT_RESPONSE_SUCCESS,
	EVENT_RESPONSE_FAIL,
	EVENT_RESPONSE_WARNING,
	EVENT_RESPONSE_TIMEOUT,
	EVENT_RESPONSE_EMPTY,
	EVENT_RESPONSE_FULL,
	EVENT_RESPONSE_AVAILABLE,
	EVENT_RESPONSE_MAX
} event_response_t;

typedef struct {
	event_process_t src;
	event_process_t dst;
	event_request_t request;
	event_response_t response;		
	union {
		struct {
			uint8_t aid;
			uint8_t mac[6];
		} client;
		struct {
			struct stats_ip_napt napt_stats;
			multi_heap_info_t heap_dram;
			multi_heap_info_t heap_psram;
		} health;
		struct {
			bool update;
			uint8_t r;
			uint8_t g;
			uint8_t b;
			uint16_t on_time;
			uint16_t off_time;
		} led;
	} data;
	uint32_t timestamp;	
} event_t; 

typedef enum {
	SYSTEM_ERROR = -1,
	SYSTEM_OK = 0,
	SYSTEM_WARNING = 1
} system_return_t;

typedef struct {
	char ssid[32];
	uint8_t clients;
	uint16_t time;
} settings_t;

typedef struct {
	uint8_t aid;
	uint8_t mac[6];
	
	uint16_t time;
} client_t;

typedef struct {
	uint8_t num;
	client_t *client;
} client_list_t;

/* Exported variables --------------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif /* TYPEDEFS_H_ */

/***************************** END OF FILE ************************************/
