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
#include <stdbool.h>

#include "lwip/stats.h"

/* Exported Macros -----------------------------------------------------------*/


/* Exported typedef ----------------------------------------------------------*/
typedef enum {
	STATE_ALERTS_IDLE = 0,
	STATE_ALERTS_PROCESS,
	STATE_ALERTS_SIGNAL,
	
	STATE_ALERTS_MAX
} state_alerts_t;

typedef enum {
	ALERTS_IDLE_CLEAR = 0,
	ALERTS_IDLE_ONLINE,
	ALERTS_IDLE_OFFLINE,
	ALERTS_IDLE_DICONNECTED,
	
	ALERTS_IDLE_MAX
} alerts_idle_t;

typedef enum {
	ALERTS_PROCESS_CLEAR = 0,
	ALERTS_PROCESS_PROV,
	ALERTS_PROCESS_OTA,
	
	ALERTS_PROCESS_MAX
} alerts_process_t;

typedef enum {
	ALERTS_SIGNAL_CLEAR = 0,
	ALERTS_SIGNAL_SUCCESS,
	ALERTS_SIGNAL_FAIL,
	ALERTS_SIGNAL_WARNING,
	
	ALERTS_SIGNAL_MAX
} alerts_signal_t;

typedef enum {
	EVENT_TRG_BUTTON_SHORT,
	EVENT_TRG_BUTTON_MEDIUM,
	EVENT_TRG_BUTTON_LONG,
	
	EVENT_TRG_WIFI_AP_STACONNECTED,
	EVENT_TRG_WIFI_AP_STADISCONNECTED,
	EVENT_TRG_WIFI_STA_DISCONNECTED,
	
	EVENT_TRG_PROV_START,
	EVENT_TRG_PROV_END,
	EVENT_TRG_PROV_FAIL,
	
	EVENT_TRG_HEALTH_INTERNET,
	EVENT_TRG_HEALTH_NO_INTERNET,
	
	EVENT_TRG_WDT,
	EVENT_TRG_TICK,
	EVENT_TRG_IP_GOT,
	
	EVENT_TRG_MAX
} event_trg_t;
	
typedef enum {	
	EVENT_RSP_ACTIONS_RESTORE_SUCCESS,
	EVENT_RSP_ACTIONS_RESTORE_FAIL,
	
	EVENT_RSP_NETWORK_OTA_START,
	EVENT_RSP_NETWORK_OTA_SUCCESS,
	EVENT_RSP_NETWORK_OTA_FAIL,
	EVENT_RSP_NETWORK_OTA_TIMEOUT,
	EVENT_RSP_NETWORK_RECONNECT_TIMEOUT,
	
	EVENT_RSP_CLIENTS_ADD_SUCCESS,
	EVENT_RSP_CLIENTS_ADD_FAIL,
	EVENT_RSP_CLIENTS_ADD_FULL,
	EVENT_RSP_CLIENTS_REMOVE_EMPTY,
	EVENT_RSP_CLIENTS_REMOVE_AVAILABLE,
	EVENT_RSP_CLIENTS_TICK_TIMEOUT,
	
	EVENT_RSP_MAX	
} event_rsp_t;

typedef enum {
	EVENT_CMD_NO = 0,
	
	EVENT_CMD_ALERTS_IDLE_ONLINE,
	EVENT_CMD_ALERTS_IDLE_OFFLINE,
	EVENT_CMD_ALERTS_IDLE_DISCONNECTED,
	EVENT_CMD_ALERTS_IDLE_FULL,
	EVENT_CMD_ALERTS_IDLE_NO_FULL,
	EVENT_CMD_ALERTS_PROCESS_PROV,
	EVENT_CMD_ALERTS_PROCESS_OTA,
	EVENT_CMD_ALERTS_PROCESS_RECONNECT,
	EVENT_CMD_ALERTS_PROCESS_END,
	EVENT_CMD_ALERTS_SIGNAL_SUCCESS,
	EVENT_CMD_ALERTS_SIGNAL_FAIL,
	EVENT_CMD_ALERTS_SIGNAL_WARNING,
	EVENT_CMD_ALERTS_MAX,
	
	EVENT_CMD_NETWORK_OTA,
	EVENT_CMD_NETWORK_RECONNECT,
	EVENT_CMD_NETWORK_DEAUTH,
	EVENT_CMD_NETWORK_MAX,
	
	EVENT_CMD_CLIENTS_ADD,
	EVENT_CMD_CLIENTS_REMOVE,
	EVENT_CMD_CLIENTS_TICK,
	EVENT_CMD_CLIENTS_MAX,
	
	EVENT_CMD_ACTIONS_RESET,
	EVENT_CMD_ACTIONS_RESTORE,
	EVENT_CMD_ACTIONS_WDT,
	EVENT_CMD_ACTIONS_MAX,
	
	EVENT_CMD_MAX	
} event_cmd_t;

typedef struct {
	int num;
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
	} data;
	uint32_t timestamp;	
} event_t; 

typedef void (*event_cb_t)(event_t *const event);

typedef struct {
	event_trg_t event;
	event_cmd_t command;
} event_route_t;

typedef enum {
	SYSTEM_ERROR = -1,
	SYSTEM_OK = 0,
	SYSTEM_WARNING = 1
} system_return_t;

/* Exported variables --------------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif /* TYPEDEFS_H_ */

/***************************** END OF FILE ************************************/
