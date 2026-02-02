#include "sntp_manager.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <time.h>

static const char *TAG = "sntp_mgr";

static bool time_synced = false;
static TaskHandle_t sntp_task_handle = NULL;
static char server_name_buf[64] = {0};
static int sync_interval_sec = 20;
static bool smooth_mode = false;

static void time_sync_notification_cb(struct timeval *tv)
{
    time_synced = true;
    ESP_LOGI(TAG, "Thời gian đã được đồng bộ thành công");
}

static void sntp_update_task(void *pvParameters)
{
    while (1) {
        if (!time_synced) {
            ESP_LOGI(TAG, "Đang chờ đồng bộ lần đầu...");
        }

        // Gọi đồng bộ (blocking trong khoảng thời gian ngắn)
        esp_netif_sntp_sync_wait(10000 / portTICK_PERIOD_MS);

        if (time_synced) {
            time_t now;
            struct tm timeinfo;
            time(&now);
            localtime_r(&now, &timeinfo);

            char strftime_buf[64];
            strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
            ESP_LOGI(TAG, "Thời gian hiện tại: %s", strftime_buf);
        }

        // Ngủ đến lần đồng bộ tiếp theo
        vTaskDelay(pdMS_TO_TICKS(sync_interval_sec * 1000ULL));
    }

    vTaskDelete(NULL);
}

esp_err_t sntp_manager_init(const char *server_name, int sync_interval_sec_param, bool use_smooth)
{
    if (server_name == NULL || strlen(server_name) == 0) {
        ESP_LOGE(TAG, "Server name không được để trống");
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(server_name_buf, server_name, sizeof(server_name_buf) - 1);
    server_name_buf[sizeof(server_name_buf) - 1] = '\0';

    if (sync_interval_sec_param > 0) {
        sync_interval_sec = sync_interval_sec_param;
    }

    smooth_mode = use_smooth;

    // Cấu hình SNTP
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(server_name_buf);
    config.sync_cb = time_sync_notification_cb;

    if (smooth_mode) {
#ifdef CONFIG_SNTP_TIME_SYNC_METHOD_SMOOTH
        config.smooth_sync = true;
#else
        ESP_LOGW(TAG, "Smooth sync không được bật trong menuconfig, dùng immediate thay thế");
        config.smooth_sync = false;
#endif
    }

    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_sntp_init thất bại: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "SNTP khởi tạo với server: %s | smooth: %s | interval: %d giây",
             server_name_buf, smooth_mode ? "YES" : "NO", sync_interval_sec);

    // Tạo task tự động update
    BaseType_t ret = xTaskCreate(
        sntp_update_task,
        "sntp_update",
        4096,
        NULL,
        5,
        &sntp_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Tạo task SNTP thất bại");
        esp_netif_sntp_deinit();
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool sntp_manager_is_time_synced(void)
{
    return time_synced;
}

time_t sntp_manager_get_time(struct tm *timeinfo)
{
    time_t now = 0;
    time(&now);

    if (timeinfo != NULL) {
        localtime_r(&now, timeinfo);
    }

    // Nếu chưa sync thì thời gian sẽ ~1970 → có thể kiểm tra thêm
    if (now < 946684800ULL) {  // trước năm 2000
        return 0;
    }

    return now;
}

void sntp_manager_deinit(void)
{
    if (sntp_task_handle != NULL) {
        vTaskDelete(sntp_task_handle);
        sntp_task_handle = NULL;
    }

    esp_netif_sntp_deinit();
    time_synced = false;
    ESP_LOGI(TAG, "SNTP manager đã dừng");
}