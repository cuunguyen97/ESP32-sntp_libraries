#include "control_manager.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "string.h"

static const char *TAG = "ctrl_mgr";

// Mỗi entry đại diện cho 1 lịch điều khiển
typedef struct {
    bool used;              // true = đang sử dụng, false = chỗ trống
    schedule_t start;       // thời gian ON gốc
    uint32_t duration_min;
    int on_id;              // ID lịch ON trong schedule_manager
    int off_id;             // ID lịch OFF
} control_entry_t;

static control_entry_t entries[MAX_CONTROLS];
static int active_count = 0;

static control_on_callback_t  on_cb  = NULL;
static control_off_callback_t off_cb = NULL;

// Callback nội bộ gọi từ schedule_manager, truyền ID lịch control
void internal_schedule_cb(int sch_id) {
    ESP_LOGI(TAG, " control ID %d", sch_id);
    for (int i = 0; i < MAX_CONTROLS; i++) {
        if (!entries[i].used) continue;

        if (entries[i].on_id == sch_id) {
            if (on_cb) on_cb(i);  // Truyền ID lịch control
            ESP_LOGI(TAG, "ON gọi cho control ID %d", i);
            return;
        }
        if (entries[i].off_id == sch_id) {
            if (off_cb) off_cb(i);  // Truyền ID lịch control
            ESP_LOGI(TAG, "OFF gọi cho control ID %d", i);
            return;
        }
    }
}

// Tính thời gian OFF
static void calc_off(const schedule_t *start, uint32_t dur_min, schedule_t *out) {
    *out = *start;
    uint32_t total = start->minute + dur_min;
    out->minute = total % 60;
    out->hour   = (start->hour + (total / 60)) % 24;
    out->weekday = start->weekday;
    out->control = start->control;
}

// Lưu toàn bộ entries vào NVS
static esp_err_t save_to_nvs(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open("ctrl_ns", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    nvs_set_i32(h, "active", active_count);
    nvs_set_blob(h, "entries", entries, sizeof(entries));
    nvs_commit(h);
    nvs_close(h);
    return ESP_OK;
}

// Load từ NVS
static esp_err_t load_from_nvs(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open("ctrl_ns", NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    int32_t act = 0;
    nvs_get_i32(h, "active", &act);
    size_t sz = sizeof(entries);
    err = nvs_get_blob(h, "entries", entries, &sz);
    nvs_close(h);

    if (err == ESP_OK) {
        active_count = act;
    } else {
        memset(entries, 0, sizeof(entries));
        active_count = 0;
    }
    return err;
}

esp_err_t control_manager_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;

    err = load_from_nvs();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Load NVS thất bại, khởi tạo mới");
        memset(entries, 0, sizeof(entries));
        active_count = 0;
    }
    
    schedule_manager_init(internal_schedule_cb);
    for (int i = 0; i < MAX_CONTROLS; i++) {
        if (entries[i].used) {
            schedule_t off;
            calc_off(&entries[i].start, entries[i].duration_min, &off);

            schedule_add(&entries[i].start);
            schedule_add(&off);

            // Cập nhật lại ID mới sau khi add
            entries[i].on_id  = schedule_get_count() - 2;
            entries[i].off_id = schedule_get_count() - 1;
        }
    }

    ESP_LOGI(TAG, "Control manager khởi tạo, %d lịch active", active_count);
    return ESP_OK;
}

void control_register_on_callback(control_on_callback_t cb) {
    on_cb = cb;
    ESP_LOGI(TAG, "Đã đăng ký callback ON (với ID)");
}

void control_register_off_callback(control_off_callback_t cb) {
    off_cb = cb;
    ESP_LOGI(TAG, "Đã đăng ký callback OFF (với ID)");
}

int control_add(const control_schedule_t *sch) {
    if (!sch) return 3;  // invalid arg

    // Tìm slot trống trước
    int slot = -1;
    for (int i = 0; i < MAX_CONTROLS; i++) {
        if (!entries[i].used) {
            slot = i;
            break;
        }
    }
    if (slot == -1) return 2;  // full

    schedule_t off;
    calc_off(&sch->start, sch->duration_min, &off);

    // Kiểm tra trùng lịch ON mới với tất cả lịch hiện có (ON và OFF)
    for (int i = 0; i < MAX_CONTROLS; i++) {
        if (!entries[i].used) continue;

        // So sánh với lịch ON hiện có
        if (entries[i].start.hour == sch->start.hour &&
            entries[i].start.minute == sch->start.minute &&
            entries[i].start.weekday == sch->start.weekday) {
            ESP_LOGW(TAG, "Trùng lịch ON với control ID %d", i);
            return 1;  // trùng
        }

        // So sánh với lịch OFF hiện có
        schedule_t existing_off;
        calc_off(&entries[i].start, entries[i].duration_min, &existing_off);
        if (existing_off.hour == sch->start.hour &&
            existing_off.minute == sch->start.minute &&
            existing_off.weekday == sch->start.weekday) {
            ESP_LOGW(TAG, "Trùng lịch ON mới với OFF của control ID %d", i);
            return 1;
        }
    }

    // Kiểm tra thêm: lịch OFF mới có trùng với lịch hiện có không
    for (int i = 0; i < MAX_CONTROLS; i++) {
        if (!entries[i].used) continue;

        // Với ON hiện có
        if (entries[i].start.hour == off.hour &&
            entries[i].start.minute == off.minute &&
            entries[i].start.weekday == off.weekday) {
            ESP_LOGW(TAG, "Trùng lịch OFF mới với ON của control ID %d", i);
            return 1;
        }

        // Với OFF hiện có
        schedule_t existing_off;
        calc_off(&entries[i].start, entries[i].duration_min, &existing_off);
        if (existing_off.hour == off.hour &&
            existing_off.minute == off.minute &&
            existing_off.weekday == off.weekday) {
            ESP_LOGW(TAG, "Trùng lịch OFF mới với OFF của control ID %d", i);
            return 1;
        }
    }

    // Không trùng → tiếp tục add
    int ret = schedule_add(&sch->start);
    if (ret != 0) return ret;

    ret = schedule_add(&off);
    if (ret != 0) {
        schedule_delete(schedule_get_count() - 1);
        return ret;
    }

    // Ghi vào slot
    entries[slot].used = true;
    entries[slot].start = sch->start;
    entries[slot].duration_min = sch->duration_min;
    entries[slot].on_id  = schedule_get_count() - 2;
    entries[slot].off_id = schedule_get_count() - 1;

    active_count++;

    save_to_nvs();
    ESP_LOGI(TAG, "Thêm lịch control thành công tại slot %d", slot);
    return 0;
}

bool control_delete(int id) {
    if (id < 0 || id >= MAX_CONTROLS || !entries[id].used) return false;

    schedule_delete(entries[id].off_id);
    schedule_delete(entries[id].on_id);

    entries[id].used = false;
    active_count--;

    save_to_nvs();
    return true;
}

bool control_set_enable(int id, bool enable) {
    if (id < 0 || id >= MAX_CONTROLS || !entries[id].used) return false;

    schedule_set_enable(entries[id].on_id, enable);
    schedule_set_enable(entries[id].off_id, enable);

    save_to_nvs();
    return true;
}

int control_get_count(void) {
    return active_count;
}

void control_get_all(control_schedule_t *out_controls, int *out_count) {
    if (!out_controls || !out_count) return;

    // Thu thập danh sách active
    typedef struct {
        control_schedule_t sch;
        int id;
    } temp_t;

    temp_t temp[MAX_CONTROLS];
    int temp_cnt = 0;

    for (int i = 0; i < MAX_CONTROLS; i++) {
        if (entries[i].used) {
            temp[temp_cnt].sch.start = entries[i].start;
            temp[temp_cnt].sch.duration_min = entries[i].duration_min;
            temp[temp_cnt].id = i;
            temp_cnt++;
        }
    }

    // Sắp xếp theo giờ + phút
    for (int i = 0; i < temp_cnt - 1; i++) {
        for (int j = i + 1; j < temp_cnt; j++) {
            schedule_t *a = &temp[i].sch.start;
            schedule_t *b = &temp[j].sch.start;
            if (a->hour > b->hour || (a->hour == b->hour && a->minute > b->minute)) {
                temp_t t = temp[i];
                temp[i] = temp[j];
                temp[j] = t;
            }
        }
    }

    // Copy ra output
    for (int i = 0; i < temp_cnt; i++) {
        out_controls[i] = temp[i].sch;
    }
    *out_count = temp_cnt;
}

control_schedule_t *control_get_by_id(int id) {
    if (id < 0 || id >= MAX_CONTROLS || !entries[id].used) return NULL;

    static control_schedule_t temp;
    temp.start = entries[id].start;
    temp.duration_min = entries[id].duration_min;
    return &temp;
}

void control_manager_deinit(void) {
    schedule_manager_deinit();
    ESP_LOGI(TAG, "Control manager deinit");
}
