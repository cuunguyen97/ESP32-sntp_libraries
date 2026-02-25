// /* LwIP SNTP example

//    This example code is in the Public Domain (or CC0 licensed, at your option.)

//    Unless required by applicable law or agreed to in writing, this
//    software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
//    CONDITIONS OF ANY KIND, either express or implied.
// */
// #include <string.h>
// #include <time.h>
// #include <sys/time.h>
// #include "esp_system.h"
// #include "esp_event.h"
// #include "esp_log.h"
// #include "esp_attr.h"
// #include "esp_sleep.h"
// #include "nvs_flash.h"
// #include "protocol_examples_common.h"
// #include "esp_netif_sntp.h"
// #include "lwip/ip_addr.h"
// #include "esp_sntp.h"
// #include "sntp_manager.h"

// static const char *TAG = "example";

// void app_main(void)
// {
//     ESP_ERROR_CHECK( nvs_flash_init() );
//     ESP_ERROR_CHECK(esp_netif_init());
//     ESP_ERROR_CHECK( esp_event_loop_create_default() );
//     ESP_ERROR_CHECK(example_connect());
//     // Chỉ cần gọi 1 dòng này
//     ESP_ERROR_CHECK( sntp_manager_init("asia.pool.ntp.org", 20, true) );
//     // hoặc: sntp_manager_init("time.google.com", 7200, false);

//     // Sau đó ở bất kỳ đâu cũng có thể lấy giờ dễ dàng
//     while (1) {
//         if (sntp_manager_is_time_synced()) {
//             struct tm timeinfo;
//             time_t now = sntp_manager_get_time(&timeinfo);
//             // dùng timeinfo hoặc now ...
//             ESP_LOGI(TAG, "Thời gian hiện tại: %s", asctime(&timeinfo));
//         }
//         vTaskDelay(pdMS_TO_TICKS(1000));
//     }
// }
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "protocol_examples_common.h"

#include "sntp_manager.h"
#include "schedule_manager.h"
#include "control_manager.h"
#include "button.h"
static const char *TAG = "MAIN";
// Callback nhận ID
void my_on(int id) {
    ESP_LOGI("MAIN", "BẬT thiết bị - lịch control ID: %d", id);
    // gpio_set_level(RELAY_PIN, 1);
}

void my_off(int id) {
    ESP_LOGI("MAIN", "TẮT thiết bị - lịch control ID: %d", id);
    // gpio_set_level(RELAY_PIN, 0);
}

void my_callback(int id) {
    ESP_LOGI("MAIN", "Đến giờ hẹn! ID = %d", id);
}

void button_event_handler(button_event_info_t *info)
{
    ESP_LOGI("MAIN", "Button %d -> ", info->button_id);

    switch (info->event) {
        case BUTTON_EVENT_SINGLE_CLICK:
            ESP_LOGI("MAIN", "SINGLE click\n");
            break;
        case BUTTON_EVENT_DOUBLE_CLICK:
            ESP_LOGI("MAIN", "DOUBLE click\n");
            break;
        case BUTTON_EVENT_TRIPLE_CLICK:
            ESP_LOGI("MAIN", "TRIPLE click\n");
            break;
        case BUTTON_EVENT_LONG_PRESS:
            ESP_LOGI("MAIN", "LONG PRESS (%.0f s)\n", info->press_duration_ms / 1000.0);
            break;
        default:
            ESP_LOGI("MAIN", "event %d\n", info->event);
            break;
    }
}

static void print_all_schedules(void) {
    schedule_t sch_list[MAX_SCHEDULES];
    int count = 0;

    schedule_get_all(sch_list, &count);

    ESP_LOGI(TAG, "=== DANH SÁCH LỊCH HIỆN TẠI (%d lịch) ===", count);

    if (count == 0) {
        ESP_LOGI(TAG, "Hiện chưa có lịch nào được thiết lập.");
        return;
    }

    for (int i = 0; i < count; i++) {
        schedule_t *sch = &sch_list[i];

        // Xử lý chuỗi ngày trong tuần (theo thứ tự tự nhiên: CN → Thứ 2 → Thứ 7)
        char days[64] = "";
        if (sch->weekday & (1 << 0)) strcat(days, "CN ");
        if (sch->weekday & (1 << 1)) strcat(days, "T2 ");
        if (sch->weekday & (1 << 2)) strcat(days, "T3 ");
        if (sch->weekday & (1 << 3)) strcat(days, "T4 ");
        if (sch->weekday & (1 << 4)) strcat(days, "T5 ");
        if (sch->weekday & (1 << 5)) strcat(days, "T6 ");
        if (sch->weekday & (1 << 6)) strcat(days, "T7 ");

        // Xóa dấu cách thừa ở cuối nếu có
        size_t len = strlen(days);
        if (len > 0 && days[len - 1] == ' ') {
            days[len - 1] = '\0';
        }

        // Trạng thái enable/disable
        const char *status = (sch->control & 1) ? "BẬT" : "TẮT";
        const char *called = (sch->control & (1 << 1)) ? "ĐÃ GỌI" : "CHƯA GỌI";

        // In ra với định dạng đẹp, dễ đọc
        ESP_LOGI(TAG, "ID %-2d | %-6s | %02d:%02d | %-20s | %s",
                 i,
                 status,
                 sch->hour, sch->minute,
                 days[0] ? days : "Không có ngày",
                 called);
    }

    ESP_LOGI(TAG, "=====================================");
}

void app_main(void)
{
    // 1. Khởi tạo NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Network & event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 3. Kết nối WiFi/Ethernet
    ESP_ERROR_CHECK(example_connect());

    ESP_ERROR_CHECK(sntp_manager_init("asia.pool.ntp.org", 3600, true));

    // Chờ đồng bộ lần đầu (tùy chọn)
    int retry = 0;
    while (!sntp_manager_is_time_synced() && retry < 30) {
        ESP_LOGI("MAIN", "Chờ đồng bộ thời gian... (%d/30)", ++retry);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    // 5. Khởi tạo schedule
    // schedule_manager_init(my_callback);

    // // Thêm lịch mẫu
    // schedule_t s;

    // // Mỗi ngày 21:54
    // s.control = 1;
    // s.hour = 21;
    // s.minute = 45;
    // s.weekday = 0x7F;           // tất cả ngày (bit 0→6 = CN → Thứ 7)
    // int res = schedule_add(&s);

    // switch (res)
    // {
    // case 0:  ESP_LOGI(TAG, "Đã thêm lịch test thành công"); break;
    // case 1:  ESP_LOGW(TAG, "Lịch này đã tồn tại"); break;
    // case 2:  ESP_LOGE(TAG, "Đạt giới hạn số lịch"); break;
    // default: ESP_LOGE(TAG, "Lỗi khi thêm lịch"); break;
    // }
    
    // // Thứ 2, 3, 6 lúc 21:56
    // s.control = 1;
    // s.hour = 21;
    // s.minute = 47;
    // s.weekday = (1<<1) | (1<<2) | (1<<5);  // bit1=Thứ2, bit2=Thứ3, bit5=Thứ6
    // res = schedule_add(&s);

    // switch (res)
    // {
    // case 0:  ESP_LOGI(TAG, "Đã thêm lịch test thành công"); break;
    // case 1:  ESP_LOGW(TAG, "Lịch này đã tồn tại"); break;
    // case 2:  ESP_LOGE(TAG, "Đạt giới hạn số lịch"); break;
    // default: ESP_LOGE(TAG, "Lỗi khi thêm lịch"); break;
    // }

    // // Chủ nhật 21:58
    // s.control = 1;
    // s.hour = 21;
    // s.minute = 50;
    // s.weekday = (1<<0);  // chỉ Chủ nhật
    // res = schedule_add(&s);

    // switch (res)
    // {
    // case 0:  ESP_LOGI(TAG, "Đã thêm lịch test thành công"); break;
    // case 1:  ESP_LOGW(TAG, "Lịch này đã tồn tại"); break;
    // case 2:  ESP_LOGE(TAG, "Đạt giới hạn số lịch"); break;
    // default: ESP_LOGE(TAG, "Lỗi khi thêm lịch"); break;
    // }

    // -------------------------------------------------------
    // Vòng lặp in thời gian + trạng thái
    // -------------------------------------------------------
    // Ví dụ cấu hình 3 nút
    button_config_t btn_cfgs[] = {
        { .gpio_num = GPIO_NUM_0, .active_level = BUTTON_ACTIVE_HIGH, .debounce_ms = 35 }
    };
    
    // Khởi tạo
    button_init(btn_cfgs, 1);

    // Đăng ký callback cho từng nút (hoặc cho tất cả cùng một hàm)
    button_register_callback(0, button_event_handler);
    
    control_register_on_callback(my_on);
    control_register_off_callback(my_off);

    control_manager_init();


    // Thêm lịch
    control_schedule_t sch = {0};
    sch.start.control = 1;
    sch.start.hour = 21;
    sch.start.minute = 50;
    sch.start.weekday = 0x7F;  // mọi ngày
    sch.duration_min = 2;  // duy trì 5 phút

    control_add(&sch);  // ID sẽ là 0 (hoặc slot đầu trống)

    // In danh sách (đã sắp xếp)
    control_schedule_t list[MAX_CONTROLS];
    int cnt = 0;
    control_get_all(list, &cnt);

    for (int i = 0; i < cnt; i++) {
        ESP_LOGI("MAIN", "Lịch %d: %02d:%02d - duy trì %lu phút",
                 i, list[i].start.hour, list[i].start.minute, list[i].duration_min);
    }
    while (1)
    {
        if (sntp_manager_is_time_synced())
        {
            struct tm timeinfo;
            time_t now = sntp_manager_get_time(&timeinfo);

            char buf[64];
            strftime(buf, sizeof(buf), "%Y-%m-%d  %H:%M:%S  %A", &timeinfo);
            ESP_LOGI(TAG, "Thời gian hiện tại: %s", buf);
            // In danh sách lịch
            // print_all_schedules();
        }
        else
        {
            ESP_LOGI(TAG, "Đang chờ đồng bộ thời gian...");
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}