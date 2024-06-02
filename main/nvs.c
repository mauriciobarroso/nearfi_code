/**
  ******************************************************************************
  * @file           : nvs.c
  * @author         : Mauricio Barroso Benavides
  * @date           : Sep 2, 2023
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
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

/* Private macros ------------------------------------------------------------*/

/* External variables --------------------------------------------------------*/

/* Private typedef -----------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/

/* Exported functions definitions --------------------------------------------*/

/* Private function definitions ----------------------------------------------*/
esp_err_t nvs_init(void) {
	ESP_LOGI("nvs", "Initializing NVS...");

	esp_err_t ret = ESP_OK;

	/* Initialize NVS */
	ret = nvs_flash_init();

	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ret = nvs_flash_erase();

		if (ret == ESP_OK) {
			ret = nvs_flash_init();

			if (ret != ESP_OK) {
				ESP_LOGE("nvs", "Failed to initialize NVS");
				return ret;
			}
		}
		else {
			ESP_LOGE("nvs", "Failed to erase NVS");
			return ret;
		}
	}

	/* Print success message */
	ESP_LOGI("nvs", "NVS initialized successfully");

	/* Return ESP_OK */
	return ret;
}

esp_err_t nvs_erase_namespace(const char * namespace_name) {
	esp_err_t ret = ESP_OK;
	nvs_handle_t nvs_handle;

	/* Open the name space to read and write */
	ret = nvs_open(namespace_name, NVS_READWRITE, &nvs_handle);

	if (ret != ESP_OK) {
		ESP_LOGE("nvs", "Error opening namespace %s", namespace_name);
		goto end;
	}

	/* Erase the namespace */
	ret = nvs_erase_all(nvs_handle);

	if (ret != ESP_OK) {
		ESP_LOGE("nvs", "Error erasing namespace %s", namespace_name);
		goto end;
	}

	/* Commit the changes */
	ret = nvs_commit(nvs_handle);

	if (ret != ESP_OK) {
		ESP_LOGE("nvs", "Error commiting changes");
		goto end;
	}

end:
	/* Close NVS and return */
	nvs_close(nvs_handle);
	return ret;
}

//esp_err_t nvs_erase_specific_key(const char *namespace_name, const char *key) {
//	esp_err_t ret = ESP_OK;
//	nvs_handle_t nvs_handle;
//
//	/* Open the name space to read and write */
//	ret = nvs_open(namespace_name, NVS_READWRITE, &nvs_handle);
//
//	if (ret != ESP_OK) {
//		ESP_LOGE("nvs", "Error opening namespace %s", namespace_name);
//		goto end;
//	}
//
//	/* Erase the namespace */
//	ret = nvs_erase_key(namespace_name, key);
//
//	if (ret != ESP_OK) {
//		ESP_LOGE("nvs", "Error erasing namespace %s", namespace_name);
//		goto end;
//	}
//
//	/* Commit the changes */
//	ret = nvs_commit(nvs_handle);
//
//	if (ret != ESP_OK) {
//		ESP_LOGE("nvs", "Error commiting changes");
//		goto end;
//	}
//
//end:
//	/* Close NVS and return */
//	nvs_close(nvs_handle);
//	return ret;
//}

esp_err_t nvs_load_string(const char *namespace_name, const char *key, char *value) {
	esp_err_t ret = ESP_OK;

	/* Open the name space to read and write */
	nvs_handle_t nvs_handle;
	ret = nvs_open(namespace_name, NVS_READONLY, &nvs_handle);

	if (ret != ESP_OK) {
		ESP_LOGE("nvs", "Failed to open namespace %s", namespace_name);
		return ret;
	}

	/* Get value size */
	size_t value_size;
	ret = nvs_get_str(nvs_handle, key, NULL, &value_size);

	if (ret != ESP_OK){
			ESP_LOGE("nvs", "Failed to get size of key: %s", key);
			return ret;
	}

	/* Allocate memory and get value */
	ret = nvs_get_str(nvs_handle, key, value, &value_size);

	if (ret != ESP_OK){
			ESP_LOGE("nvs", "Failed to load key: %s", key);
			return ret;
	}

	/* Close NVS */
	nvs_close(nvs_handle);

	/* Return ESP_OK */
	return ret;
}

esp_err_t nvs_save_string(const char *namespace_name, const char *key, char *value) {
	esp_err_t ret = ESP_OK;

	/* Open the name space to read and write */
	nvs_handle_t nvs_handle;
	ret = nvs_open(namespace_name, NVS_READWRITE, &nvs_handle);

	if (ret != ESP_OK) {
		ESP_LOGE("nvs", "Error opening namespace %s", namespace_name);
		return ret;
	}

	/* Set value */
	ret = nvs_set_str(nvs_handle, key, value);

	if (ret != ESP_OK){
		ESP_LOGE("nvs", "Failed to save key: %s", key);
		return ret;
	}

	ret = nvs_commit(nvs_handle);

	if (ret != ESP_OK){
		ESP_LOGE("nvs", "Failed to commit changes for key: %s", key);
		return ret;
	}

	/* Return ESP_OK */
	return ret;
}

esp_err_t nvs_load_blob(const char *namespace_name, const char *key, void *value) {
	esp_err_t ret = ESP_OK;

	/* Open the name space to read and write */
	nvs_handle_t nvs_handle;
	ret = nvs_open(namespace_name, NVS_READONLY, &nvs_handle);

	if (ret != ESP_OK) {
		ESP_LOGE("nvs", "Failed to open namespace %s", namespace_name);
		return ret;
	}

	/* Get value size */
	size_t value_size;
	ret = nvs_get_blob(nvs_handle, key, NULL, &value_size);

	if (ret != ESP_OK){
			ESP_LOGE("nvs", "Failed to get size of key: %s", key);
			return ret;
	}

	/* Allocate memory and get value */
	ret = nvs_get_blob(nvs_handle, key, value, &value_size);

	if (ret != ESP_OK){
			ESP_LOGE("nvs", "Failed to load key: %s", key);
			return ret;
	}

	/* Close NVS */
	nvs_close(nvs_handle);

	/* Return ESP_OK */
	return ret;
}

esp_err_t nvs_save_blob(const char *namespace_name, const char *key, void *value, size_t size) {
	esp_err_t ret = ESP_OK;

	/* Open the name space to read and write */
	nvs_handle_t nvs_handle;
	ret = nvs_open(namespace_name, NVS_READWRITE, &nvs_handle);

	if (ret != ESP_OK) {
		ESP_LOGE("nvs", "Failed to open namespace %s", namespace_name);
		return ret;
	}

	/* Set value */
	ret = nvs_set_blob(nvs_handle, key, value, size);

	if (ret != ESP_OK){
		ESP_LOGE("nvs", "Failed to save key: %s", key);
		return ret;
	}

	ret = nvs_commit(nvs_handle);

	if (ret != ESP_OK){
		ESP_LOGE("nvs", "Failed to commit changes for key: %s", key);
		return ret;
	}

	return ret;
}

/***************************** END OF FILE ************************************/
