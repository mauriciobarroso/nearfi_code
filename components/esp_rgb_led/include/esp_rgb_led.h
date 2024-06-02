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
#include "freertos/queue.h"

#include "led_strip.h"

/* Exported macro ------------------------------------------------------------*/

/* Exported typedef ----------------------------------------------------------*/
typedef struct {
	uint8_t r;
	uint8_t g;
	uint8_t b;
} esp_rgb_led_color_t;

typedef enum {
	ESP_RGB_LED_MODE_CONTINUOUS = 0,
	ESP_RGB_LED_MODE_BLINK,
	ESP_RGB_LED_MODE_FADE,
	ESP_RGB_LED_MODE_OFF,
	ESP_RGB_LED_MODE_WIT_FUOTA,
	ESP_RGB_LED_MODE_WIT_ALEXA_REQUEST,
	ESP_RGB_LED_MODE_MAX
} esp_rgb_led_mode_t;

typedef enum {
	ESP_RGB_LED_BLINK_STATE_ON = 0,
	ESP_RGB_LED_BLINK_STATE_OFF = 1
} esp_rgb_led_blink_state_t;

typedef enum {
	ESP_RGB_LED_FADE_STATE_RISING = 0,
	ESP_RGB_LED_FADE_STATE_FALLING = 1,
	ESP_RGB_LED_FADE_STATE_INACTIVE = 2
} esp_rgb_led_fade_state_t;

typedef struct {
	led_strip_handle_t led_handle;
	uint32_t gpio_num;
	uint16_t led_num;
	TimerHandle_t timer_handle;
	esp_rgb_led_blink_state_t blink_state;
	esp_rgb_led_fade_state_t fade_state;
	esp_rgb_led_color_t color;
	esp_rgb_led_mode_t mode;
	TaskHandle_t task_handle;
	uint16_t on_time;
	uint16_t off_time;
	float on_delta_r;
	float on_delta_g;
	float on_delta_b;
	float off_delta_r;
	float off_delta_g;
	float off_delta_b;
	uint16_t counter;
	uint16_t on_steps;
	uint16_t off_steps;
	esp_rgb_led_color_t current_color;
} esp_rgb_led_t;
/* Exported variables --------------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/
/**
  * @brief Function to initialize a RGB LED instance
  *
  * @param me              : Pointer to a esp_rgb_led_t structure
  * @param gpio            : GPIO number to drive the RGB LEDs
  * @param led_num         : RGB LEDs number
  * @param task_priority   :
  * @param task_stack_size :
  *
  * @retval
  * 	- ESP_OK on success
  * 	- ESP_FAIL on fail
  */
esp_err_t esp_rgb_led_init(esp_rgb_led_t * const me, uint32_t gpio_num,
		uint16_t led_num);

/**
  * @brief Function to ...
  */
void esp_rgb_led_set_continuos(esp_rgb_led_t *const me, uint8_t r, uint8_t g,
		uint8_t b);

/**
  * @brief Function to ...
  */
void esp_rgb_led_set_blink(esp_rgb_led_t *const me, uint8_t r, uint8_t g,
		uint8_t b, uint16_t on_time, uint16_t off_time);

/**
  * @brief Function to ...
  */
void esp_rgb_led_set_fade(esp_rgb_led_t *const me, uint8_t r, uint8_t g,
		uint8_t b, uint16_t on_time, uint16_t off_time);

/**
  * @brief Function to ...
  */
void esp_rgb_led_set_off(esp_rgb_led_t *const me);

#ifdef __cplusplus
}
#endif

#endif /* ESP_RGB_LED_H_ */

/***************************** END OF FILE ************************************/
