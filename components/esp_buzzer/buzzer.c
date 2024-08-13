/**
  ******************************************************************************
  * @file           : buzzer.c
  * @author         : Mauricio Barroso Benavides
  * @date           : Aug 13, 2024
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

/* Includes ------------------------------------------------------------------*/
#include "buzzer.h"
#include "esp_log.h"

/* Private macro -------------------------------------------------------------*/

/* External variables --------------------------------------------------------*/

/* Private typedef -----------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
static uint32_t tone_index = 0;

/* Tag for debug */
static const char * TAG = "buzzer";

/* Private function prototypes -----------------------------------------------*/
static void timer_handler(TimerHandle_t timer);

/* Exported functions --------------------------------------------------------*/
void buzzer_init(buzzer_t *const me, gpio_num_t gpio,
		ledc_timer_t timer, ledc_channel_t channel)
{
	ESP_LOGI(TAG, "Intializing buzzer instance...");

	me->ledc_channel = channel;
	me->gpio = gpio;
	me->ledc_timer = timer;

	ledc_timer_config_t timer_config = {
			.speed_mode = LEDC_LOW_SPEED_MODE,
			.timer_num = me->ledc_timer,
			.duty_resolution = LEDC_TIMER_10_BIT,
			.freq_hz = 4000,
			.clk_cfg = LEDC_AUTO_CLK
	};
	ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

	ledc_channel_config_t channel_config = {
			.speed_mode = LEDC_LOW_SPEED_MODE,
			.channel = me->ledc_channel,
			.timer_sel = me->ledc_timer,
			.intr_type = LEDC_INTR_DISABLE,
			.gpio_num = me->gpio,
			.duty = 0,
			.hpoint = 0
	};
	ESP_ERROR_CHECK(ledc_channel_config(&channel_config));

	me->timer_handle = xTimerCreate("Passive Timer Buzzer", 1, pdFALSE,
			(void*)me, timer_handler);
}

void buzzer_set_freq(buzzer_t *const me, uint32_t freq)
{
	ESP_ERROR_CHECK(ledc_set_freq(LEDC_LOW_SPEED_MODE, me->ledc_channel, freq));
}

void buzzer_set_volume(buzzer_t *const me, uint32_t volume)
{
	ESP_ERROR_CHECK(
			ledc_set_duty(LEDC_LOW_SPEED_MODE, me->ledc_channel,
					(uint32_t )(volume * 5.12)));
	ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, me->ledc_channel));
}

void buzzer_run(buzzer_t *const me, sound_t *data,
		size_t data_len)
{
	tone_index = 0;
	me->sound_buf.data = data;
	me->sound_buf.len = data_len;
	xTimerChangePeriod(me->timer_handle, 1, 0);
}
/* Private functions ---------------------------------------------------------*/
static void timer_handler(TimerHandle_t timer)
{
	buzzer_t *passive_buzzer = (buzzer_t*)pvTimerGetTimerID(
			timer);

	if (tone_index < passive_buzzer->sound_buf.len) {
		buzzer_set_volume(passive_buzzer,
				passive_buzzer->sound_buf.data[tone_index].volume);
		buzzer_set_freq(passive_buzzer,
				passive_buzzer->sound_buf.data[tone_index].tone);
		xTimerChangePeriod(passive_buzzer->timer_handle,
				pdMS_TO_TICKS(passive_buzzer->sound_buf.data[tone_index].time),
				0);
		tone_index++; /* Go to next array position */
	}
	else {
		tone_index = 0;
		buzzer_set_volume(passive_buzzer, 0);
		xTimerStop(passive_buzzer->timer_handle, 0);
	}
}

/***************************** END OF FILE ************************************/
