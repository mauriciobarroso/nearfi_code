#include "stdio.h"
#include "stdlib.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "driver/ledc.h"

uint16_t GL_BuzzerAllNotes[] = {
		261, 277, 294, 311, 329, 349, 370, 392, 415, 440, 466, 494,
		523, 554, 587, 622, 659, 698, 740, 784, 831, 880, 932, 988,
		1046, 1109, 1175, 1245, 1319, 1397, 1480, 1568, 1661, 1760, 1865, 1976,
		2093, 2217, 2349, 2489, 2637, 2794, 2960, 3136, 3322, 3520, 3729, 3951,
		4186, 4434, 4699, 4978, 5274, 5588, 5920, 6272, 6645, 7040, 7459, 7902};

uint32_t tone_index = 0;

#define OCTAVE_ONE_START_INDEX		(0)
#define OCTAVE_TWO_START_INDEX		(OCTAVE_ONE_START_tone_index + 12)
#define OCTAVE_THREE_START_INDEX	(OCTAVE_TWO_START_tone_index + 12)
#define OCTAVE_FOUR_START_INDEX		(OCTAVE_THREE_START_tone_index + 12)
#define OCTAVE_FIVE_START_INDEX		(OCTAVE_FOUR_START_tone_index + 12)
#define BUZZER_DEFAULT_FREQ				(4186) //C8 - 5th octave "Do"
#define BUZZER_DEFAULT_DURATION		(20) //20ms
#define BUZZER_VOLUME_MAX					(10)
#define BUZZER_VOLUME_MUTE				(0)

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
} passive_buzzer_t;



static void timer_handler(TimerHandle_t timer);

void passive_buzzer_init(passive_buzzer_t *const me, gpio_num_t gpio, ledc_timer_t timer, ledc_channel_t channel) {
	me->ledc_channel = channel;
	me->gpio = gpio;
	me->ledc_timer= timer;

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

	me->timer_handle = xTimerCreate("Passive Timer Buzzer", 1, pdFALSE, (void *)me, timer_handler);
}

void passive_buzzer_set_freq(passive_buzzer_t *const me, uint32_t freq) {
	ESP_ERROR_CHECK(ledc_set_freq(LEDC_LOW_SPEED_MODE, me->ledc_channel, freq));
}

void passive_buzzer_set_volume(passive_buzzer_t *const me, uint32_t volume) {
	ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, me->ledc_channel, (uint32_t)(volume * 5.12)));
	ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, me->ledc_channel));
}

void passive_buzzer_run(passive_buzzer_t *const me, sound_t *data, size_t data_len) {
	tone_index = 0;
	me->sound_buf.data = data;
	me->sound_buf.len = data_len; /* fixme: get data_len from data */
	xTimerChangePeriod(me->timer_handle, 1, 0);
}

static void timer_handler(TimerHandle_t timer) {
	passive_buzzer_t *passive_buzzer = (passive_buzzer_t *)pvTimerGetTimerID(timer);

	if (tone_index < passive_buzzer->sound_buf.len) {
		passive_buzzer_set_volume(passive_buzzer, passive_buzzer->sound_buf.data[tone_index].volume);
		passive_buzzer_set_freq(passive_buzzer, passive_buzzer->sound_buf.data[tone_index].tone);
		xTimerChangePeriod(passive_buzzer->timer_handle, pdMS_TO_TICKS(passive_buzzer->sound_buf.data[tone_index].time), 0);
		tone_index++; /* Go to next array position */
	}
	else {
		tone_index = 0;
		passive_buzzer_set_volume(passive_buzzer, 0);
		xTimerStop(passive_buzzer->timer_handle, 0);
	}
}
