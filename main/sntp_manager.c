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

static void time_sync_notification_cb(struct timeval *tv)
{
    time_synced = true;
    ESP_LOGI(TAG, "Thời gian đã được đồng bộ thành công");
}

esp_err_t sntp_manager_init(const char *server_name, int sync_interval_sec, bool use_smooth)
{
    if (server_name == NULL || strlen(server_name) == 0) {
        ESP_LOGE(TAG, "Server name không được để trống");
        return ESP_ERR_INVALID_ARG;
    }

    // Cấu hình SNTP
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(server_name);
    config.sync_cb = time_sync_notification_cb;

    if (use_smooth) {
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

    // Thiết lập interval đồng bộ định kỳ (mặc định 1 giờ nếu không truyền tham số)
    uint32_t interval_ms = (sync_interval_sec > 0) ? sync_interval_sec * 1000UL : 3600 * 1000UL;
    // Đảm bảo không nhỏ hơn 15 giây theo RFC
    if (interval_ms < 15000) {
        interval_ms = 15000;
        ESP_LOGW(TAG, "Interval quá nhỏ, tự động đặt thành 15 giây (tối thiểu theo RFC)");
    }
    sntp_set_sync_interval(interval_ms);

    ESP_LOGI(TAG, "SNTP khởi tạo với server: %s | smooth: %s | interval: %lu giây",
             server_name, use_smooth ? "YES" : "NO", interval_ms / 1000);

    // Tự động set múi giờ Việt Nam
    setenv("TZ", "CST-7", 1);
    tzset();
    ESP_LOGI(TAG, "Đã thiết lập múi giờ Việt Nam (UTC+7)");

    // Không cần task riêng nữa – SNTP tự poll định kỳ

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

    // Nếu thời gian quá cũ → coi như chưa sync
    if (now < 946684800ULL) {  // trước năm 2000
        return 0;
    }

    return now;
}

void sntp_manager_deinit(void)
{
    esp_netif_sntp_deinit();
    time_synced = false;
    ESP_LOGI(TAG, "SNTP manager đã dừng");
}