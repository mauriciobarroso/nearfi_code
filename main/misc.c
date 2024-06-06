/**
  ******************************************************************************
  * @file           : misc.c
  * @author         : Mauricio Barroso Benavides
  * @date           : Sep 3, 2023
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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_https_ota.h"

/* Private macros ------------------------------------------------------------*/

/* External variables --------------------------------------------------------*/

/* Private typedef -----------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/

/* Exported functions definitions --------------------------------------------*/

/* Private function definitions ----------------------------------------------*/
void reset_device(void *arg)
{
	ESP_LOGW("misc", "Restarting device...");
	vTaskDelay(pdMS_TO_TICKS(1000));
	esp_restart();
}

void error_handler(void)
{
	for (;;) {
		vTaskDelay(pdMS_TO_TICKS(1000)); /* Wait for 1 second */
	}
}

esp_err_t ota_update(const char *ota_url, const char *ota_cert,
		uint32_t timeout_ms)
{
	ESP_LOGI("misc", "Starting firmware update...");

	esp_err_t ret = ESP_OK;

	/* Fill http client and ota configuration structures */
	esp_http_client_config_t http_client_config = {
			.url = ota_url,
			.cert_pem = ota_cert,
	};

	esp_https_ota_config_t ota_config = {
			.http_config = &http_client_config,
	};

	/* Perform HTTPS OTA firmware update */
	esp_https_ota_handle_t https_ota_handle = NULL;
	ret = esp_https_ota_begin(&ota_config, &https_ota_handle);
	if (https_ota_handle == NULL) {
		return ESP_FAIL;
	}

	TickType_t initial_time = xTaskGetTickCount();
	while (1) {
		ret = esp_https_ota_perform(https_ota_handle);
		if (ret != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
			break;
		}
		TickType_t elapsed_time = xTaskGetTickCount() - initial_time;
		if (pdMS_TO_TICKS(elapsed_time) > timeout_ms) {
			return ESP_ERR_TIMEOUT;
		}
	}

	if (ret != ESP_OK) {
		esp_https_ota_abort(https_ota_handle);
		return ret;
	}

	ret = esp_https_ota_finish(https_ota_handle);

	if (ret != ESP_OK) {
		ESP_LOGE("misc", "Failed to update firmware");
		return ret;
	}

	ESP_LOGI("misc", "Firmware updated successfully");

	/* Return ESP_OK */
	return ret;
}

/***************************** END OF FILE ************************************/
