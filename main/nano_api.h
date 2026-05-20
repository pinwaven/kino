#ifndef NANO_API_H
#define NANO_API_H

#include "esp_err.h"
#include <stddef.h>

esp_err_t nano_api_get_chip(const char *chip_id);
esp_err_t nano_api_post_mock_biomarkers(void);
esp_err_t nano_api_post_kino_result(void);
const char *nano_api_last_error_message(void);
const char *nano_api_last_upload_summary(void);

#endif
