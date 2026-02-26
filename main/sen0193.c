#include "sen0193.h"
#include "esp_log.h"
#include "esp_adc_cal.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SEN0193";

static void sen0193_reading_task(void *arg);

esp_err_t sen0193_start(sen0193_handle_t *handle,
                        adc1_channel_t channel,
                        adc_atten_t atten,
                        uint32_t read_interval_ms)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Reset handle
    handle->channel = channel;
    handle->atten = atten;
    handle->state = SEN0193_STATE_INITIALIZING;
    handle->task_running = false;
    handle->read_interval_ticks = pdMS_TO_TICKS(read_interval_ms);
    handle->has_valid_calibration = false;
    handle->dry_value = 0;
    handle->wet_value = 0;
    handle->last_raw = 0;
    handle->last_voltage_mv = 0;
    handle->last_moisture_percent = -1;

    // Cấu hình ADC
    esp_err_t ret = adc1_config_width(ADC_WIDTH_BIT_12);
    if (ret != ESP_OK) return ret;

    ret = adc1_config_channel_atten(channel, atten);
    if (ret != ESP_OK) return ret;

    // Khởi tạo calibration characteristics
    handle->adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    if (handle->adc_chars == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_adc_cal_characterize(ADC_UNIT_1, atten, ADC_WIDTH_BIT_12, 1100, handle->adc_chars);

    // Load calibration từ NVS
    nvs_handle_t nvs;
    ret = nvs_open(SEN0193_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (ret == ESP_OK) {
        uint32_t dry = 0, wet = 0;
        if (nvs_get_u32(nvs, SEN0193_NVS_KEY_DRY, &dry) == ESP_OK &&
            nvs_get_u32(nvs, SEN0193_NVS_KEY_WET, &wet) == ESP_OK) {
            
            bool valid = (dry > wet) &&
                         (dry >= SEN0193_MIN_VALID_DRY_VALUE) &&
                         (wet >= SEN0193_MIN_VALID_WET_VALUE) &&
                         (dry - wet >= SEN0193_MIN_DRY_WET_DIFFERENCE);

            if (valid) {
                handle->dry_value = dry;
                handle->wet_value = wet;
                handle->has_valid_calibration = true;
                handle->state = SEN0193_STATE_READY_CALIBRATED;
            }
        }
        nvs_close(nvs);
    }

    if (handle->state == SEN0193_STATE_INITIALIZING) {
        handle->state = SEN0193_STATE_READY_NOT_CALIBRATED;
    }

    // Khởi động task đọc dữ liệu
    handle->task_running = true;
    BaseType_t task_ret = xTaskCreate(sen0193_reading_task,
                                      "sen0193_task",
                                      3072,
                                      handle,
                                      5,
                                      NULL);
    if (task_ret != pdPASS) {
        free(handle->adc_chars);
        handle->adc_chars = NULL;
        handle->task_running = false;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "SEN0193 started → state: %s", sen0193_state_to_string(handle->state));
    return ESP_OK;
}

esp_err_t sen0193_stop(sen0193_handle_t *handle)
{
    if (!handle || !handle->task_running) {
        return ESP_OK;
    }

    handle->task_running = false;
    vTaskDelay(pdMS_TO_TICKS(150));  // đợi task dừng an toàn

    if (handle->adc_chars) {
        free(handle->adc_chars);
        handle->adc_chars = NULL;
    }

    handle->state = SEN0193_STATE_UNINITIALIZED;
    ESP_LOGI(TAG, "SEN0193 stopped");
    return ESP_OK;
}

static void sen0193_reading_task(void *arg)
{
    sen0193_handle_t *handle = (sen0193_handle_t *)arg;

    while (handle->task_running)
    {
        uint32_t sum = 0;
        for (int i = 0; i < SEN0193_READING_SAMPLES; i++)
        {
            sum += adc1_get_raw(handle->channel);
            vTaskDelay(pdMS_TO_TICKS(SEN0193_SAMPLE_DELAY_MS));
        }
        uint32_t raw = sum / SEN0193_READING_SAMPLES;

        uint32_t voltage = esp_adc_cal_raw_to_voltage(raw, handle->adc_chars);

        int percent = -1;
        if (handle->has_valid_calibration)
        {
            if (raw >= handle->dry_value) {
                percent = 0;
            } else if (raw <= handle->wet_value) {
                percent = 100;
            } else {
                int64_t numerator = (int64_t)(handle->dry_value - raw) * 100LL;
                int64_t denominator = (int64_t)(handle->dry_value - handle->wet_value);
                percent = (int)(numerator / denominator);
                if (percent < 0) percent = 0;
                if (percent > 100) percent = 100;
            }
        }

        // Cập nhật giá trị mới nhất
        handle->last_raw = raw;
        handle->last_voltage_mv = voltage;
        handle->last_moisture_percent = percent;

        vTaskDelay(handle->read_interval_ticks);
    }

    vTaskDelete(NULL);
}

sen0193_state_t sen0193_get_state(const sen0193_handle_t *handle)
{
    return handle ? handle->state : SEN0193_STATE_UNINITIALIZED;
}

const char* sen0193_state_to_string(sen0193_state_t state)
{
    switch (state) {
        case SEN0193_STATE_UNINITIALIZED:           return "UNINITIALIZED";
        case SEN0193_STATE_INITIALIZING:            return "INITIALIZING";
        case SEN0193_STATE_READY_NOT_CALIBRATED:    return "READY_NOT_CALIBRATED";
        case SEN0193_STATE_CALIBRATING_DRY:         return "CALIBRATING_DRY";
        case SEN0193_STATE_CALIBRATING_WET:         return "CALIBRATING_WET";
        case SEN0193_STATE_READY_CALIBRATED:        return "READY_CALIBRATED";
        case SEN0193_STATE_ERROR:                   return "ERROR";
        default:                                    return "UNKNOWN";
    }
}

esp_err_t sen0193_start_calibrate_dry(sen0193_handle_t *handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;

    sen0193_state_t current = handle->state;
    if (current != SEN0193_STATE_READY_NOT_CALIBRATED &&
        current != SEN0193_STATE_READY_CALIBRATED &&
        current != SEN0193_STATE_ERROR) {
        return ESP_ERR_INVALID_STATE;
    }

    handle->state = SEN0193_STATE_CALIBRATING_DRY;
    ESP_LOGI(TAG, "Bắt đầu calib DRY - đặt cảm biến trong không khí khô");
    return ESP_OK;
}

esp_err_t sen0193_start_calibrate_wet(sen0193_handle_t *handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;

    if (handle->state != SEN0193_STATE_CALIBRATING_DRY) {
        return ESP_ERR_INVALID_STATE;
    }

    handle->state = SEN0193_STATE_CALIBRATING_WET;
    ESP_LOGI(TAG, "Bắt đầu calib WET - ngâm cảm biến vào nước sạch");
    return ESP_OK;
}

esp_err_t sen0193_finish_calibration(sen0193_handle_t *handle)
{
    if (!handle || handle->state != SEN0193_STATE_CALIBRATING_WET) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t dry_sum = 0;
    uint32_t wet_sum = 0;
    int count = 0;

    for (int i = 0; i < SEN0193_CALIBRATION_SAMPLES; i++)
    {
        if (handle->last_raw > 0) {  // tránh giá trị 0 bất thường
            if (handle->state == SEN0193_STATE_CALIBRATING_DRY) {
                dry_sum += handle->last_raw;
            } else if (handle->state == SEN0193_STATE_CALIBRATING_WET) {
                wet_sum += handle->last_raw;
            }
            count++;
        }
        vTaskDelay(pdMS_TO_TICKS(SEN0193_CALIBRATION_SAMPLE_DELAY_MS));
    }

    if (count == 0) {
        handle->state = SEN0193_STATE_ERROR;
        return ESP_FAIL;
    }

    uint32_t dry = dry_sum / count;
    uint32_t wet = wet_sum / count;

    bool valid = (dry > wet) &&
                 (dry >= SEN0193_MIN_VALID_DRY_VALUE) &&
                 (wet >= SEN0193_MIN_VALID_WET_VALUE) &&
                 ((dry - wet) >= SEN0193_MIN_DRY_WET_DIFFERENCE);

    if (!valid)
    {
        handle->state = SEN0193_STATE_ERROR;
        ESP_LOGE(TAG, "Calibration không hợp lệ: dry=%lu wet=%lu", dry, wet);
        return ESP_ERR_INVALID_STATE;
    }

    // Lưu giá trị
    handle->dry_value = dry;
    handle->wet_value = wet;
    handle->has_valid_calibration = true;
    handle->state = SEN0193_STATE_READY_CALIBRATED;

    // Lưu vào NVS
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(SEN0193_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK)
    {
        nvs_set_u32(nvs, SEN0193_NVS_KEY_DRY, dry);
        nvs_set_u32(nvs, SEN0193_NVS_KEY_WET, wet);
        nvs_commit(nvs);
        nvs_close(nvs);
        ESP_LOGI(TAG, "Đã lưu calibration: dry=%lu  wet=%lu", dry, wet);
    }
    else
    {
        ESP_LOGE(TAG, "Không thể mở NVS để lưu calibration");
    }

    return ESP_OK;
}

void sen0193_get_latest_values(const sen0193_handle_t *handle,
                               uint32_t *raw_out,
                               uint32_t *voltage_mv_out,
                               int *moisture_percent_out)
{
    if (!handle) return;

    if (raw_out)             *raw_out             = handle->last_raw;
    if (voltage_mv_out)      *voltage_mv_out      = handle->last_voltage_mv;
    if (moisture_percent_out) *moisture_percent_out = handle->last_moisture_percent;
}

esp_err_t sen0193_clear_calibration(sen0193_handle_t *handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(SEN0193_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK)
    {
        nvs_erase_key(nvs, SEN0193_NVS_KEY_DRY);
        nvs_erase_key(nvs, SEN0193_NVS_KEY_WET);
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    handle->has_valid_calibration = false;
    handle->dry_value = 0;
    handle->wet_value = 0;
    handle->state = SEN0193_STATE_READY_NOT_CALIBRATED;

    ESP_LOGI(TAG, "Đã xóa calibration trong NVS");
    return ESP_OK;
}