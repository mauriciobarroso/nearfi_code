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
static void esp_rgb_led_task(void *arg);

/* Exported functions --------------------------------------------------------*/
/**
  * @brief Function to initialize a RGB LED instance
  */
esp_err_t esp_rgb_led_init(esp_rgb_led_t * const me, uint32_t gpio_num,
		uint16_t led_num) {
	ESP_LOGI(TAG, "Initializing RGB LED instance...");

	esp_err_t ret = ESP_OK;

	/* Fill the members with the default values */
	me->gpio_num = gpio_num;
	me->led_num = led_num;
	me->blink_state = ESP_RGB_LED_BLINK_STATE_OFF;
	me->fade_state = ESP_RGB_LED_FADE_STATE_INACTIVE;
	me->mode = ESP_RGB_LED_MODE_OFF;

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

	/* Create RTOS task */
	if (xTaskCreate(esp_rgb_led_task,
			"ESP RGB LED Task",
			configMINIMAL_STACK_SIZE * 2,
			(void *)me,
			tskIDLE_PRIORITY + 1,
			&me->task_handle) != pdPASS) {

		ESP_LOGE(TAG, "Failed to allocate memory to create task");
		return ESP_ERR_NO_MEM;
	}

	ESP_LOGI(TAG, "Done ");

	/* Return ESP_OK */
	return ret;
}

/**
  * @brief Function to set the color of all RGB LEDs
  */
void esp_rgb_led_set(esp_rgb_led_t * const me, uint8_t r, uint8_t g, uint8_t b) {
	/* Turning on all the RGB LEDs */
	for (uint8_t i = 0; i < me->led_num; i++) {
		led_strip_set_pixel(me->led_handle, i, r, g, b);
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
  * @brief Function to ...
  */
void esp_rgb_led_set_continuos(esp_rgb_led_t *const me, uint8_t r, uint8_t g,
		uint8_t b) {
	/* Check if the new operating mode is different than the current */
	if (me->mode == ESP_RGB_LED_MODE_CONTINUOUS) {
		/* Check if the new color is different than the current */
		if (me->color.r == r && me->color.g == g && me->color.b == b) {
			return;
		}
	}
	/* Update the new operating mode and color */
	else {
		me->mode = ESP_RGB_LED_MODE_CONTINUOUS;
	}

	/* Update the new color */
	me->color.r = r;
	me->color.g = g;
	me->color.b = b;

	/* Notify to the task to set the new values or operating mode */
	xTaskNotifyGive(me->task_handle);
}

/**
  * @brief Function to ...
  */
void esp_rgb_led_set_blink(esp_rgb_led_t *const me, uint8_t r, uint8_t g,
		uint8_t b, uint16_t on_time, uint16_t off_time) {
	/* Check if the new operating mode is different than the current */
	if (me->mode == ESP_RGB_LED_MODE_BLINK) {
		/* Check if the new color is different than the current */
		if (me->color.r == r && me->color.g == g && me->color.b == b) {
			return;
		}
	}
	/* Update the new operating mode and color */
	else {
		me->mode = ESP_RGB_LED_MODE_BLINK;
	}

	/* Update the new color */
	me->color.r = r;
	me->color.g = g;
	me->color.b = b;

	/* update the new times */
	me->on_time = on_time;
	me->off_time = off_time;

	/**/
	me->blink_state = ESP_RGB_LED_BLINK_STATE_ON;

	/* Notify to the task to set the new values or operating mode */
	xTaskNotifyGive(me->task_handle);
}

/**
  * @brief Function to ...
  */
void esp_rgb_led_set_fade(esp_rgb_led_t *const me, uint8_t r, uint8_t g,
		uint8_t b, uint16_t on_time, uint16_t off_time) {
	/* Check if the new operating mode is different than the current */
	if (me->mode == ESP_RGB_LED_MODE_FADE) {
		/* Check if the new color is different than the current */
		if (me->color.r == r && me->color.g == g && me->color.b == b) {
			return;
		}
	}
	/* Update the new operating mode and color */
	else {
		me->mode = ESP_RGB_LED_MODE_FADE;
	}

	/* Update the new color */
	me->color.r = r;
	me->color.g = g;
	me->color.b = b;

	/* Update the new color */
	me->current_color.r = 0;
	me->current_color.g = 0;
	me->current_color.b = 0;

	/* update the new times */
	me->on_time = on_time;
	me->off_time = off_time;

	/* Calculate the on and off steps */
	me->on_steps = on_time / 10;
	me->off_steps = off_time / 10;

	/* Calculate the color deltas */
	me->on_delta_r = (float)(me->color.r) / me->on_steps;
	me->on_delta_g = (float)(me->color.g) / me->on_steps;
	me->on_delta_b = (float)(me->color.b) / me->on_steps;

	me->off_delta_r = (float)(me->color.r) / me->off_steps;
	me->off_delta_g = (float)(me->color.g) / me->off_steps;
	me->off_delta_b = (float)(me->color.b) / me->off_steps;

	/* Reset on and off counters */
	me->counter = 0;

	/**/
	me->fade_state = ESP_RGB_LED_FADE_STATE_RISING;

	/* Notify to the task to set the new values or operating mode */
	xTaskNotifyGive(me->task_handle);
}

/**
  * @brief Function to ...
  */
void esp_rgb_led_set_off(esp_rgb_led_t *const me) {
	/* Check if the new operating mode is different than the current */
	if (me->mode == ESP_RGB_LED_MODE_OFF) {
		return;
	}
	/* Update the new operating mode */
	else {
		me->mode = ESP_RGB_LED_MODE_OFF;
	}

	/* Notify to the task to set the new values or operating mode */
	xTaskNotifyGive(me->task_handle);
}

/* Private functions ---------------------------------------------------------*/
static void esp_rgb_led_task(void *arg) {
	esp_rgb_led_t *rgb_led = (esp_rgb_led_t *)arg;
	TickType_t ticks_to_wait = pdMS_TO_TICKS(10);

	for (;;) {
		ulTaskNotifyTake(pdFALSE, ticks_to_wait);

		switch (rgb_led->mode) {
		case ESP_RGB_LED_MODE_CONTINUOUS:
				esp_rgb_led_set(rgb_led, rgb_led->color.r, rgb_led->color.g, rgb_led->color.b);
				ticks_to_wait = portMAX_DELAY;
				break;

			case ESP_RGB_LED_MODE_BLINK:
				if (rgb_led->blink_state == ESP_RGB_LED_BLINK_STATE_ON) {
					esp_rgb_led_set(rgb_led, rgb_led->color.r, rgb_led->color.g, rgb_led->color.b);
					rgb_led->blink_state = ESP_RGB_LED_BLINK_STATE_OFF;
					ticks_to_wait = pdMS_TO_TICKS(rgb_led->on_time);
				}
				else {
					esp_rgb_led_clear(rgb_led);
					rgb_led->blink_state = ESP_RGB_LED_BLINK_STATE_ON;
					ticks_to_wait = pdMS_TO_TICKS(rgb_led->off_time);
				}

				break;

			case ESP_RGB_LED_MODE_FADE:
				esp_rgb_led_set(rgb_led,
						rgb_led->current_color.r,
						rgb_led->current_color.g,
						rgb_led->current_color.b);

				if (rgb_led->fade_state == ESP_RGB_LED_FADE_STATE_RISING) {
					rgb_led->current_color.r = rgb_led->on_delta_r * rgb_led->counter;
					rgb_led->current_color.g = rgb_led->on_delta_g * rgb_led->counter;
					rgb_led->current_color.b = rgb_led->on_delta_b * rgb_led->counter;

					if (rgb_led->counter++ >= rgb_led->on_steps) {
						rgb_led->counter = rgb_led->off_steps; /*  */
						rgb_led->fade_state = ESP_RGB_LED_FADE_STATE_FALLING;
					}
				}
				else {
					rgb_led->current_color.r = rgb_led->off_delta_r * rgb_led->counter;
					rgb_led->current_color.g = rgb_led->off_delta_g * rgb_led->counter;
					rgb_led->current_color.b = rgb_led->off_delta_b * rgb_led->counter;

					if (rgb_led->counter-- <= 0) {
						rgb_led->counter = 0;
						rgb_led->fade_state = ESP_RGB_LED_FADE_STATE_RISING;
					}
				}

				ticks_to_wait = pdMS_TO_TICKS(10);
				break;

			case ESP_RGB_LED_MODE_OFF:
				esp_rgb_led_clear(rgb_led);
				ticks_to_wait = portMAX_DELAY;
				break;

			default:
				rgb_led->mode = ESP_RGB_LED_MODE_OFF;
				break;
		}
	}
}

/***************************** END OF FILE ************************************/
