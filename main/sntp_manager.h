#ifndef SNTP_MANAGER_H
#define SNTP_MANAGER_H

#include <esp_err.h>

#include <stdbool.h>
#include <time.h>

/**
 * @brief Khởi tạo SNTP client + task tự động đồng bộ định kỳ
 * 
 * @param server_name   Tên server NTP (ví dụ: "pool.ntp.org", "asia.pool.ntp.org", "time.google.com")
 * @param sync_interval Giây giữa 2 lần đồng bộ (ví dụ: 3600 = 1 giờ)
 *                        Nếu <= 0 thì dùng giá trị mặc định của SNTP (thường ~1 giờ)
 * @param use_smooth    true = dùng smooth adjustment (thời gian điều chỉnh dần)
 *                      false = nhảy thẳng (immediate)
 * 
 * @return ESP_OK nếu khởi tạo thành công
 *         ESP_FAIL hoặc mã lỗi khác nếu thất bại
 */
esp_err_t sntp_manager_init(const char *server_name, int sync_interval_sec, bool use_smooth);

/**
 * @brief Kiểm tra xem thời gian đã được đồng bộ thành công chưa
 * 
 * @return true nếu đã sync thành công ít nhất một lần
 */
bool sntp_manager_is_time_synced(void);

/**
 * @brief Lấy thời gian hiện tại (đã điều chỉnh múi giờ nếu có)
 *        Chỉ là wrapper tiện lợi cho time() + localtime_r()
 * 
 * @param[out] timeinfo  Con trỏ tới struct tm (có thể NULL nếu không cần)
 * @return thời gian unix timestamp (time_t), hoặc 0 nếu chưa sync
 */
time_t sntp_manager_get_time(struct tm *timeinfo);

/**
 * @brief Dừng task SNTP và deinit (thường không cần gọi trừ khi muốn tắt hẳn)
 */
void sntp_manager_deinit(void);

#endif // SNTP_MANAGER_H