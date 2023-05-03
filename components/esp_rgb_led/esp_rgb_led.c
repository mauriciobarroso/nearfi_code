/**
  ******************************************************************************
  * @file           : esp_rgb_led.c
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

/* Includes ------------------------------------------------------------------*/
#include "esp_rgb_led.h"
#include "esp_log.h"

/* Private macro -------------------------------------------------------------*/

/* External variables --------------------------------------------------------*/

/* Private typedef -----------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
static const char * TAG = "rgb_led";

/* Private function prototypes -----------------------------------------------*/
static void timer_handler(TimerHandle_t timer);

/* Exported functions --------------------------------------------------------*/
/**
  * @brief Function to initialize a RGB LED instance
  */
esp_err_t esp_rgb_led_init(esp_rgb_led_t * const me, uint32_t gpio_num, uint16_t led_num) {
	ESP_LOGI(TAG, "Initializing RGB LED instance...");

	esp_err_t ret = ESP_OK;

	/* Fill the members with the default values */
	me->gpio_num = gpio_num;
	me->led_num = led_num;
	me->led_state = true;

	/* Configure the PGIO and the RGB LEDs number */
	led_strip_config_t rgb_led_config = {
			.strip_gpio_num = me->gpio_num,
			.max_leds = me->led_num,
			.led_pixel_format = LED_PIXEL_FORMAT_GRB,
			.led_model = LED_MODEL_WS2812,
			.flags.invert_out = false
	};

	/* Configure RMT ticks resolution */
	led_strip_rmt_config_t rmt_config = {
			.clk_src = RMT_CLK_SRC_DEFAULT,
			.resolution_hz = 10 * 1000 * 1000,
			.flags.with_dma = false
	};

	ret = led_strip_new_rmt_device(&rgb_led_config, &rmt_config, &me->led_handle);

	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Error creating a new RMT device");
		return ret;
	}

	/* Clear all RGB LEDs */
	ret = led_strip_clear(me->led_handle);

	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Error turning off the RGB LEDs");
		return ret;
	}

	/* Create a timer to generate the blink effect */
	me->timer_handle = xTimerCreate("RGB LED Timer",
			1,
			pdTRUE,
			(void *)me,
			timer_handler);

	ESP_LOGI(TAG, "Done");

	return ret;
}

/**
  * @brief Function to set the color of all RGB LEDs
  */
void esp_rgb_led_set(esp_rgb_led_t * const me, uint8_t r, uint8_t g, uint8_t b) {
	/* Set the instance structure RGB values */
	me->rgb.r = r;
	me->rgb.g = g;
	me->rgb.b = b;

	/* Turning on all the RGB LEDs */
	for (uint8_t i = 0; i < me->led_num; i++) {
		led_strip_set_pixel(me->led_handle, i, me->rgb.r, me->rgb.g, me->rgb.b);
	}

	led_strip_refresh(me->led_handle);
}

/**
  * @brief Function to clear all RGB LEDs
  */
void esp_rgb_led_clear(esp_rgb_led_t * const me) {
	led_strip_clear(me->led_handle);
}

/**
  * @brief Function to start the blink operation
  */
void esp_rgb_led_blink_start(esp_rgb_led_t * const me, uint16_t time, uint8_t r, uint8_t g, uint8_t b) {
	me->rgb.r = r;
	me->rgb.g = g;
	me->rgb.b = b;

	xTimerChangePeriod(me->timer_handle,
			pdMS_TO_TICKS(time),
			0);
	xTimerStart(me->timer_handle, 0);

	esp_rgb_led_set(me, me->rgb.r, me->rgb.g, me->rgb.b);
}

/**
  * @brief Function to stop the blink operation
  */
void esp_rgb_led_blink_stop(esp_rgb_led_t * const me) {
	xTimerStop(me->timer_handle, 0);
}

/* Private functions ---------------------------------------------------------*/
static void timer_handler(TimerHandle_t timer) {
	esp_rgb_led_t * rgb_led = (esp_rgb_led_t *)pvTimerGetTimerID(timer);

	rgb_led->led_state = !rgb_led->led_state;

	if (rgb_led->led_state) {
		esp_rgb_led_set(rgb_led, rgb_led->rgb.r, rgb_led->rgb.g, rgb_led->rgb.b);
	}
	else {
		led_strip_clear(rgb_led->led_handle);
	}
}

/***************************** END OF FILE ************************************/
