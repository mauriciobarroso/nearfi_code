/**
  ******************************************************************************
  * @file           : esp_buzzer.h
  * @author         : Mauricio Barroso Benavides
  * @date           : Feb 27, 2023
  * @brief          : todo: write brief 
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef ESP_BUZZER_H_
#define ESP_BUZZER_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>

#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

/* Exported macro ------------------------------------------------------------*/

/* Exported typedef ----------------------------------------------------------*/
/* Buzzer data type */
typedef struct {
	gpio_num_t gpio;
	TimerHandle_t timer_handle;
	bool state;
	uint16_t on_time;
	uint16_t off_time;
	uint8_t times;
} esp_buzzer_t;

/* Exported variables --------------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/
/**
  * @brief Initialize a buzzer instance
  *
  * @param me   : Pointer to a esp_buzzer_t structure
  * @param gpio : GPIO number to drive the buzzer
  *
  * @retval
  * 	- ESP_OK on success
  * 	- ESP_FAIL on fail
  */
esp_err_t esp_buzzer_init(esp_buzzer_t * const me, gpio_num_t gpio);

/**
  * @brief Start a buzzer instance
  *
  * @param me       : Pointer to a esp_buzzer_t structure
  * @param on_time  : GPIO number to drive the buzzer
  * @param off_time : GPIO number to drive the buzzer
  * @param times    : GPIO number to drive the buzzer
  */
void esp_buzzer_start(esp_buzzer_t * const me, uint16_t on_time, uint16_t off_time, uint8_t times);

/**
  * @brief Stop a buzzer instance
  *
  * @param me : Pointer to a esp_buzzer_t structure
  */
void esp_buzzer_stop(esp_buzzer_t * const me);

#ifdef __cplusplus
}
#endif

#endif /* ESP_BUZZER_H_ */

/***************************** END OF FILE ************************************/
