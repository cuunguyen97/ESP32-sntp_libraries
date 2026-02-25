#ifndef SCHEDULE_MANAGER_H
#define SCHEDULE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

// Cấu trúc lịch (4 bytes)
typedef struct {
    uint8_t control;   // Bit 0: enable (1) / disable (0)
                       // Bit 1: đã gọi callback trong ngày hôm nay (1 = đã gọi)
    uint8_t hour;      // 0-23
    uint8_t minute;    // 0-59
    uint8_t weekday;   // Bit 0: CN (tm_wday=0), Bit 1: Thứ 2 (tm_wday=1), ..., Bit 6: Thứ 7 (tm_wday=6)
} schedule_t;

// Callback khi đến giờ hẹn, nhận ID của lịch
typedef void (*schedule_callback_t)(int id);

// Số lịch tối đa
#define MAX_SCHEDULES 20

/**
 * @brief Khởi tạo schedule manager
 * Yêu cầu: sntp_manager đã được init trước và thời gian đã sync
 * 
 * @param cb Callback khi lịch khớp
 * @return ESP_OK nếu thành công
 */
esp_err_t schedule_manager_init(schedule_callback_t cb);

/**
 * @brief Thêm một lịch mới
 * @param new_sch Con trỏ đến schedule_t cần thêm
 * @return 
 *   0 = thành công
 *   1 = trùng lịch (giờ + phút + weekday giống hệt)
 *   2 = đã đạt max
 *   3 = lỗi khác
 */
int schedule_add(const schedule_t *new_sch);

/**
 * @brief Xóa lịch theo ID
 * @param id ID (0 đến số lịch hiện tại - 1)
 * @return true = thành công, false = ID không hợp lệ
 */
bool schedule_delete(int id);

/**
 * @brief Bật/tắt lịch
 * @param id ID lịch
 * @param enable true = bật, false = tắt
 * @return true = thành công, false = ID không hợp lệ
 */
bool schedule_set_enable(int id, bool enable);

/**
 * @brief Lấy toàn bộ danh sách lịch
 * @param out_schedules Buffer để copy (phải cấp phát đủ MAX_SCHEDULES)
 * @param out_count Số lượng lịch thực tế
 */
void schedule_get_all(schedule_t *out_schedules, int *out_count);

/**
 * @brief Lấy lịch theo ID
 * @param id ID
 * @return Con trỏ đến schedule_t (trong RAM), NULL nếu không hợp lệ
 */
schedule_t *schedule_get_by_id(int id);

/**
 * @brief Số lượng lịch hiện tại
 */
int schedule_get_count(void);

/**
 * @brief Dừng task (nếu cần)
 */
void schedule_manager_deinit(void);

#endif // SCHEDULE_MANAGER_H