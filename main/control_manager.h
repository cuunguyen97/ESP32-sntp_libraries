#ifndef CONTROL_MANAGER_H
#define CONTROL_MANAGER_H

#include <esp_err.h>
#include <stdint.h>
#include <stdbool.h>
#include "schedule_manager.h"


#define MAX_CONTROLS (MAX_SCHEDULES / 2)
// Cấu trúc lịch điều khiển (input/output)
typedef struct {
    schedule_t start;       // Thời gian bắt đầu (ON)
    uint32_t duration_min;  // Thời gian duy trì (phút) → để tính OFF
} control_schedule_t;

// Callback cho ON và OFF (bây giờ nhận thêm ID lịch)
typedef void (*control_on_callback_t)(int id);
typedef void (*control_off_callback_t)(int id);

/**
 * @brief Khởi tạo control manager
 * - Load dữ liệu từ NVS
 * - Yêu cầu schedule_manager đã init trước
 * @return ESP_OK nếu thành công
 */
esp_err_t control_manager_init(void);

/**
 * @brief Đăng ký callback cho ON (nhận ID lịch)
 * @param cb Hàm callback sẽ gọi khi đến giờ ON, kèm ID lịch
 */
void control_register_on_callback(control_on_callback_t cb);

/**
 * @brief Đăng ký callback cho OFF (nhận ID lịch)
 * @param cb Hàm callback sẽ gọi khi đến giờ OFF, kèm ID lịch
 */
void control_register_off_callback(control_off_callback_t cb);

/**
 * @brief Thêm lịch điều khiển mới
 * - Tìm slot trống (used = false)
 * - Tạo lịch ON và OFF trong schedule_manager
 * - Lưu flash
 * @return 0: thành công, 1: trùng lịch, 2: full, 3: lỗi khác
 */
int control_add(const control_schedule_t *sch);

/**
 * @brief Xóa lịch theo ID
 * - Đánh dấu used = false, không dịch mảng
 * - Xóa lịch ON và OFF trong schedule_manager
 * - Lưu flash
 */
bool control_delete(int id);

/**
 * @brief Bật/tắt lịch (cả ON và OFF)
 */
bool control_set_enable(int id, bool enable);

/**
 * @brief Số lượng lịch đang active
 */
int control_get_count(void);

/**
 * @brief Lấy toàn bộ danh sách lịch control (đã sắp xếp theo giờ/phút)
 * @param out_controls Buffer để copy (phải cấp phát đủ MAX_CONTROLS)
 * @param out_count Số lượng thực tế
 */
void control_get_all(control_schedule_t *out_controls, int *out_count);

/**
 * @brief Lấy lịch control theo ID (không sắp xếp)
 * @return con trỏ đến control_schedule_t (static, dùng ngay), NULL nếu không hợp lệ
 */
control_schedule_t *control_get_by_id(int id);

/**
 * @brief Giải phóng
 */
void control_manager_deinit(void);

#endif // CONTROL_MANAGER_H