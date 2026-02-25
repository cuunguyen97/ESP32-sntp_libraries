#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUTTON_MAX_COUNT            5
#define BUTTON_DEFAULT_DEBOUNCE_MS  30
#define BUTTON_DEFAULT_CLICK_TIMEOUT_MS  350
#define BUTTON_DEFAULT_LONG_PRESS_MS     5000

// Các sự kiện nút nhấn
typedef enum {
    BUTTON_EVENT_SINGLE_CLICK = 1,      // 1 lần nhấn
    BUTTON_EVENT_DOUBLE_CLICK,          // 2 lần
    BUTTON_EVENT_TRIPLE_CLICK,
    BUTTON_EVENT_QUAD_CLICK,
    BUTTON_EVENT_QUINT_CLICK,           // 5 lần
    BUTTON_EVENT_LONG_PRESS,            // nhấn giữ dài
    BUTTON_EVENT_PRESSED,               // vừa nhấn xuống (tùy chọn)
    BUTTON_EVENT_RELEASED,              // vừa nhả ra (tùy chọn)
} button_event_t;

// Cấu trúc thông tin sự kiện gửi về callback
typedef struct {
    uint8_t     button_id;      // 0..4
    button_event_t event;
    uint32_t    click_count;    // số lần click (khi multi-click)
    uint32_t    press_duration_ms;  // thời gian giữ (ms) - hữu ích cho long press
} button_event_info_t;

// Prototype hàm callback
typedef void (*button_callback_t)(button_event_info_t *info);

// Cấu hình cho mỗi nút
typedef struct {
    gpio_num_t gpio_num;
    bool active_level;          // 1 = active high, 0 = active low (thường là 0)
    uint16_t debounce_ms;
    uint16_t multi_click_timeout_ms;
    uint16_t long_press_threshold_ms;
} button_config_t;

// Khởi tạo thư viện nút nhấn
// Truyền mảng cấu hình và số lượng nút (tối đa 5)
esp_err_t button_init(const button_config_t *configs, uint8_t button_count);

// Đăng ký callback cho một nút cụ thể
// button_id từ 0 đến button_count-1
esp_err_t button_register_callback(uint8_t button_id, button_callback_t cb);

// Gỡ callback
esp_err_t button_unregister_callback(uint8_t button_id);

// Hủy toàn bộ module (ít dùng)
void button_deinit(void);

// Các macro tiện lợi để cấu hình nhanh
#define BUTTON_ACTIVE_LOW   false
#define BUTTON_ACTIVE_HIGH  true

#ifdef __cplusplus
}
#endif

#endif // BUTTON_H