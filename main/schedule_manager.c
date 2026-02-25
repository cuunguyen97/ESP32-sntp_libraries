#include "schedule_manager.h"
#include "sntp_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"
#include "time.h"

static const char *TAG = "sch_mgr";

static schedule_t schedules[MAX_SCHEDULES];
static int num_schedules = 0;
static schedule_callback_t global_cb = NULL;
static TaskHandle_t check_task_handle = NULL;
static int last_wday = -1;
static bool update_flag = true;

// Task kiểm tra lịch định kỳ
static void schedule_check_task(void *arg) {
    while (1) {
        struct tm timeinfo;
        time_t now = sntp_manager_get_time(&timeinfo);
        if (now == 0) {
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            continue;
        }

        int cur_wday = timeinfo.tm_wday;

        // Sang ngày mới → reset cờ "đã gọi" cho tất cả lịch
        if (cur_wday != last_wday) {
            for (int i = 0; i < num_schedules; i++) {
                schedules[i].control &= ~(1 << 1);  // xóa bit called
            }
            last_wday = cur_wday;
            update_flag = true;
            ESP_LOGI(TAG, "Sang ngày mới (wday=%d) → reset cờ called", cur_wday);
        }

        if (update_flag) {
            update_flag = false;
            // Không cần sort vì không lưu flash và add đã sắp xếp
        }

        // Kiểm tra từng lịch
        for (int i = 0; i < num_schedules; i++) {
            schedule_t *sch = &schedules[i];

            // Bỏ qua nếu disable hoặc đã gọi trong ngày
            if ((sch->control & 1) == 0) continue;
            if (sch->control & (1 << 1)) continue;

            // Kiểm tra ngày trong tuần (bit tương ứng với tm_wday)
            if ((sch->weekday & (1 << cur_wday)) == 0) continue;

            // Kiểm tra giờ
            if (sch->hour != timeinfo.tm_hour) continue;

            // Kiểm tra phút (gọi khi phút hiện tại trong khoảng [minute → minute+2])
            int min_diff = timeinfo.tm_min - sch->minute;
            if (min_diff >= 0 && min_diff <= 1) {
                sch->control |= (1 << 1);  // đánh dấu đã gọi
                if (global_cb) {
                    global_cb(i);
                }
                ESP_LOGI(TAG, "Gọi callback lịch ID=%d  (%02d:%02d) wday=%d", 
                         i, sch->hour, sch->minute, cur_wday);
                break;  // chỉ gọi 1 lịch mỗi lần quét (theo yêu cầu cũ)
            }
            
        }

        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

esp_err_t schedule_manager_init(schedule_callback_t cb) {
    global_cb = cb;
    num_schedules = 0;
    last_wday = -1;

    // Lấy ngày hiện tại để khởi tạo last_wday
    struct tm ti;
    time_t now = sntp_manager_get_time(&ti);
    if (now != 0) {
        last_wday = ti.tm_wday;
    }

    // Reset cờ called ban đầu
    for (int i = 0; i < num_schedules; i++) {
        schedules[i].control &= ~(1 << 1);
    }

    // Tạo task
    BaseType_t res = xTaskCreate(schedule_check_task, "sch_check", 4096, NULL, 5, &check_task_handle);
    if (res != pdPASS) {
        ESP_LOGE(TAG, "Tạo task thất bại");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Schedule manager khởi tạo thành công (RAM only)");
    return ESP_OK;
}

int schedule_add(const schedule_t *new_sch) {
    if (num_schedules >= MAX_SCHEDULES) return 2;

    // Kiểm tra trùng lịch
    for (int i = 0; i < num_schedules; i++) {
        if (schedules[i].hour == new_sch->hour &&
            schedules[i].minute == new_sch->minute &&
            schedules[i].weekday == new_sch->weekday) {
            return 1;
        }
    }

    // Thêm và reset cờ called
    schedules[num_schedules] = *new_sch;
    schedules[num_schedules].control &= ~(1 << 1);
    num_schedules++;

    update_flag = true;
    return 0;
}

bool schedule_delete(int id) {
    if (id < 0 || id >= num_schedules) return false;

    // Dịch mảng
    for (int i = id; i < num_schedules - 1; i++) {
        schedules[i] = schedules[i + 1];
    }
    num_schedules--;

    update_flag = true;
    return true;
}

bool schedule_set_enable(int id, bool enable) {
    if (id < 0 || id >= num_schedules) return false;

    if (enable) {
        schedules[id].control |= 1;
    } else {
        schedules[id].control &= ~1;
    }
    schedules[id].control &= ~(1 << 1);  // reset called khi thay đổi trạng thái

    update_flag = true;
    return true;
}

void schedule_get_all(schedule_t *out_schedules, int *out_count) {
    if (out_schedules) {
        memcpy(out_schedules, schedules, num_schedules * sizeof(schedule_t));
    }
    if (out_count) {
        *out_count = num_schedules;
    }
}

schedule_t *schedule_get_by_id(int id) {
    if (id < 0 || id >= num_schedules) return NULL;
    return &schedules[id];
}

int schedule_get_count(void) {
    return num_schedules;
}

void schedule_manager_deinit(void) {
    if (check_task_handle) {
        vTaskDelete(check_task_handle);
        check_task_handle = NULL;
    }
    ESP_LOGI(TAG, "Schedule manager deinit");
}