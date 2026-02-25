#ifndef SNTP_MANAGER_H
#define SNTP_MANAGER_H

#include <esp_err.h>
#include <stdbool.h>
#include <time.h>

/**
 * @brief Khởi tạo SNTP client với server NTP chỉ định
 * 
 * - Sử dụng esp_netif_sntp để đồng bộ thời gian tự động định kỳ
 * - Không cần task thủ công, SNTP tự poll theo interval đã set
 * - Mặc định interval: 3600 giây (1 giờ) nếu không truyền tham số
 * - Tự động set múi giờ Việt Nam (UTC+7) bằng "CST-7"
 * 
 * @param server_name   Tên server NTP (ví dụ: "asia.pool.ntp.org", "pool.ntp.org", "time.google.com")
 * @param sync_interval_sec Khoảng thời gian đồng bộ định kỳ (giây)
 *                          - Nếu <= 0: dùng mặc định 3600 giây (1 giờ)
 *                          - Khuyến nghị: 3600–7200 giây để tránh tải server
 * @param use_smooth    true = dùng smooth adjustment (điều chỉnh dần thời gian)
 *                      false = dùng immediate (nhảy thẳng đến thời gian chính xác)
 * 
 * @return ESP_OK nếu khởi tạo thành công
 *         ESP_FAIL hoặc mã lỗi khác nếu thất bại
 */
esp_err_t sntp_manager_init(const char *server_name, int sync_interval_sec, bool use_smooth);

/**
 * @brief Kiểm tra xem thời gian đã được đồng bộ thành công chưa (ít nhất một lần)
 * 
 * @return true nếu đã sync thành công, false nếu chưa
 */
bool sntp_manager_is_time_synced(void);

/**
 * @brief Lấy thời gian hiện tại (Unix timestamp) và struct tm đã điều chỉnh múi giờ
 * 
 * @param[out] timeinfo Con trỏ tới struct tm (có thể NULL nếu không cần)
 *                      Nếu truyền vào, sẽ được điền giờ địa phương (UTC+7)
 * @return thời gian unix timestamp (time_t)
 *         trả về 0 nếu chưa đồng bộ hoặc thời gian không hợp lệ (< năm 2000)
 */
time_t sntp_manager_get_time(struct tm *timeinfo);

/**
 * @brief Dừng SNTP client và giải phóng tài nguyên
 *        Thường không cần gọi trừ khi muốn tắt hoàn toàn SNTP
 */
void sntp_manager_deinit(void);

#endif // SNTP_MANAGER_H