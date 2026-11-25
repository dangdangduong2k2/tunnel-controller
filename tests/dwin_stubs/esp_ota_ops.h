#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NOT_FOUND 1
enum esp_ota_img_states_t { ESP_OTA_IMG_NEW, ESP_OTA_IMG_PENDING_VERIFY, ESP_OTA_IMG_VALID, ESP_OTA_IMG_UNDEFINED, ESP_OTA_IMG_INVALID };
struct esp_partition_t {};
const esp_partition_t *esp_ota_get_running_partition();
esp_err_t esp_ota_get_state_partition(const esp_partition_t *, esp_ota_img_states_t *);
