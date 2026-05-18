#ifndef POCT_API_H
#define POCT_API_H

#include "esp_err.h"

esp_err_t poct_api_verify_card(const char *code);
esp_err_t poct_api_upload_mock_result(const char *code);

#endif
