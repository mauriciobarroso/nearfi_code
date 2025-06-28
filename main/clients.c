/**
 ******************************************************************************
 * @file           : clients.c
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
#include <stdlib.h>
#include <string.h>

/* Private macros ------------------------------------------------------------*/

/* External variables --------------------------------------------------------*/

/* Private typedef -----------------------------------------------------------*/
typedef struct {
  uint8_t aid;
  uint8_t mac[6];
  uint16_t time;
} client_t;

typedef struct {
  uint8_t num;
  client_t *client;
} clients_t;

/* Private variables ---------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/

/* Exported functions definitions --------------------------------------------*/
void clients_init(clients_t *const me) {
  me->num = 0;
  me->client = NULL;
}

void clients_add(clients_t *const me, uint8_t *mac, uint8_t aid, uint16_t time) {
  /* Reallocate memory for the new client */
  me->client =
      (client_t *)realloc(me->client, (me->num + 1) * sizeof(client_t));

  if (me->client == NULL) {
    return;
  }

  /* Fill the new client data */
  strncpy((char *)me->client[me->num].mac, (char *)mac, 6);
  me->client[me->num].time = time;
  me->client[me->num].aid = aid;

  /* Increase the clients number */
  me->num++;
}

void clients_remove(clients_t *const me, uint8_t *mac) {
  /* Search for the client with the same MAC address */
  uint8_t idx = 0;

  while (idx < me->num) {
    if (!strncmp((char *)me->client[idx].mac, (char *)mac, 6)) {
      /* Shift the clients after the removed index */
      for (uint8_t i = idx; i < me->num - 1; i++) {
        me->client[i] = me->client[i + 1];
      }

      /* Reduce the clients number */
      me->num--;

      /* Reallocate memory */
      me->client = (client_t *)realloc(me->client, me->num * sizeof(client_t));

      return;
    }

    idx++;
  }
}

/* Private function definitions ----------------------------------------------*/

/***************************** END OF FILE ************************************/
