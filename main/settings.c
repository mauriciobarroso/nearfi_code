/**
 ******************************************************************************
 * @file           : settings.c
 * @author         : Mauricio Barroso Benavides
 * @date           : Jun, 2025
 * @brief          : todo: write brief
 ******************************************************************************
 * @attention
 *
 * MIT License
 *
 * Copyright (c) 2025 Mauricio Barroso Benavides
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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Private macros ------------------------------------------------------------*/
#define SETTINGS_EEPROM_ADDR 0x0
#define SETTINGS_SSID_DEFAULT "NearFi"
#define SETTINGS_CLIENTS_DEFAULT 15
#define SETTINGS_TIME_DEFAULT 60000

/* External variables --------------------------------------------------------*/

/* Private typedef -----------------------------------------------------------*/
typedef int (*settings_write_t)(uint8_t data_addr, uint8_t *data,
                                uint32_t data_len);
typedef int (*settings_read_t)(uint8_t data_addr, uint8_t *data,
                               uint32_t data_len);

typedef struct {
  char ssid[32];
  uint8_t clients_num;
  uint16_t time;
} settings_data_t;

typedef struct {
  settings_data_t data;
  settings_read_t read;
  settings_write_t write;
} settings_t;

/* Private variables ---------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/

/* Exported functions definitions --------------------------------------------*/
int settings_init(settings_t *const me, settings_read_t read,
                  settings_write_t write) {

  me->read = read;
  me->write = write;

  return 0;
}

void settings_set_ssid(settings_t *const me, const char *ssid) {
  strcpy(me->data.ssid, ssid);
}

void settings_set_clients(settings_t *const me, uint8_t clients) {
  me->data.clients_num = clients;
}

void settings_set_time(settings_t *const me, uint16_t time) {
  me->data.time = time;
}

char *settings_get_ssid(settings_t *const me) { return me->data.ssid; }

uint8_t settings_get_clients(settings_t *const me) {
  return me->data.clients_num;
}

uint16_t settings_get_time(settings_t *const me) { return me->data.time; }

bool settings_save(settings_t *const me) {
  if (me->write(SETTINGS_EEPROM_ADDR, (uint8_t *)&me->data, sizeof(settings_data_t)) != 0) {
    return false;
  }

  return true;
}

bool settings_load(settings_t *const me) {
  if (me->read(SETTINGS_EEPROM_ADDR, (uint8_t *)&me->data, sizeof(settings_data_t)) != 0) {
    return false;
  }

  if (*((uint8_t *)me) == 0xFF) {
    settings_set_ssid(me, SETTINGS_SSID_DEFAULT);
    settings_set_clients(me, SETTINGS_CLIENTS_DEFAULT);
    settings_set_time(me, SETTINGS_TIME_DEFAULT);

    if (!settings_save(me)) {
      return false;
    }
  }

  return true;
}

/* Private function definitions ----------------------------------------------*/

/***************************** END OF FILE ************************************/
