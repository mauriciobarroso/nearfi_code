/**
  ******************************************************************************
  * @file           : esp_rgb_led.h
  * @author         : Mauricio Barroso Benavides
  * @date           : Feb 28, 2023
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
#ifndef ESP_RGB_LED_H_
#define ESP_RGB_LED_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "led_strip.h"

/* Exported macro ------------------------------------------------------------*/

/* Exported typedef ----------------------------------------------------------*/
typedef struct {
	uint8_t r;
	uint8_t g;
	uint8_t b;
} rgb_t;

typedef struct {
	led_strip_handle_t led_handle;
	uint32_t gpio_num;
	uint16_t led_num;
	TimerHandle_t timer_handle;
	bool led_state;
	rgb_t rgb;
} esp_rgb_led_t;
/* Exported variables --------------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/
/**
  * @brief Function to initialize a RGB LED instance
  *
  * @param me      : Pointer to a esp_rgb_led_t structure
  * @param gpio    : GPIO number to drive the RGB LEDs
  * @param led_num : RGB LEDs number
  *
  * @retval
  * 	- ESP_OK on success
  * 	- ESP_FAIL on fail
  */
esp_err_t esp_rgb_led_init(esp_rgb_led_t * const me, uint32_t gpio_num, uint16_t led_num);

/**
  * @brief Function to set the color of all RGB LEDs
  *
  * @param me : Pointer to a esp_rgb_led_t structure
  * @param r  : Red color value
  * @param g  : Green color value
  * @param b  : Blue color value
  */
void esp_rgb_led_set(esp_rgb_led_t * const me, uint8_t r, uint8_t g, uint8_t b);

/**
  * @brief Function to clear all RGB LEDs
  *
  * @param me : Pointer to a esp_rgb_led_t structure
  */
void esp_rgb_led_clear(esp_rgb_led_t * const me);

/**
  * @brief Function to start the blink operation
  *
  * @param me   : Pointer to a esp_rgb_led_t structure
  * @param time : Blink time in miliseconds
  * @param r    : Red color value
  * @param g    : Green color value
  * @param b    : Blue color value
  */
void esp_rgb_led_blink_start(esp_rgb_led_t * const me, uint16_t time, uint8_t r, uint8_t g, uint8_t b);

/**
  * @brief Function to stop the blink operation
  *
  * @param me : Pointer to a esp_rgb_led_t structure
  */
void esp_rgb_led_blink_stop(esp_rgb_led_t * const me);

#ifdef __cplusplus
}
#endif

#endif /* ESP_RGB_LED_H_ */

/***************************** END OF FILE ************************************/
