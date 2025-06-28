/**
 ******************************************************************************
 * @file           : buzzer.h
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef BUZZER_H_
#define BUZZER_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stdio.h"
#include "stdlib.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "driver/ledc.h"

/* Exported macro ------------------------------------------------------------*/

/* Exported typedef ----------------------------------------------------------*/
typedef struct {
  uint32_t tone;
  uint32_t time;
  uint32_t volume;
} sound_t;

typedef struct {
  sound_t *data;
  size_t len;
} sound_buf_t;

typedef struct {
  ledc_timer_t ledc_timer;
  ledc_channel_t ledc_channel;
  gpio_num_t gpio;
  TimerHandle_t timer_handle;
  sound_buf_t sound_buf;
} buzzer_t;

/* Exported variables --------------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/
void buzzer_init(buzzer_t *const me, gpio_num_t gpio, ledc_timer_t timer,
                 ledc_channel_t channel);

void buzzer_set_freq(buzzer_t *const me, uint32_t freq);

void buzzer_set_volume(buzzer_t *const me, uint32_t volume);

void buzzer_run(buzzer_t *const me, sound_t *data, size_t data_len);

#ifdef __cplusplus
}
#endif

#endif /* BUZZER_H_ */

/***************************** END OF FILE ************************************/
