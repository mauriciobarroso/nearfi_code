/**
  ******************************************************************************
  * @file           : typedefs.h
  * @author         : Mauricio Barroso Benavides
  * @date           : May 27, 2024
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
#ifndef TYPEDEFS_H_
#define TYPEDEFS_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

/* Exported Macros -----------------------------------------------------------*/


/* Exported typedef ----------------------------------------------------------*/
typedef enum {
	SYSTEM_STATE_BOOT = 0,
	SYSTEM_STATE_PROV,
	SYSTEM_STATE_CONNECTED,
	SYSTEM_STATE_DISCONNECTED,
	SYSTEM_STATE_FULL,
	SYSTEM_STATE_OTA,
	SYSTEM_STATE_MAX
} system_state_t;


typedef enum {
	SYSTEM_ERROR = -1,
	SYSTEM_OK = 0,
	SYSTEM_WARNING = 1
} system_return_t;

/* Exported variables --------------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif /* TYPEDEFS_H_ */

/***************************** END OF FILE ************************************/
