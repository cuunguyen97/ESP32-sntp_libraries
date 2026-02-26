#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUTTON_MAX_COUNT                5
#define BUTTON_DEFAULT_DEBOUNCE_MS      30
#define BUTTON_DEFAULT_CLICK_TIMEOUT_MS 350
#define BUTTON_DEFAULT_LONG_PRESS_MS    2000
#define BUTTON_DEFAULT_HOLD_TICK_MS     1000
#define BUTTON_POLLING_INTERVAL_MS      10     // mới: chu kỳ quét GPIO

// Sự kiện click
typedef enum {
    BUTTON_CLICK_SINGLE = 1,
    BUTTON_CLICK_DOUBLE,
    BUTTON_CLICK_TRIPLE,
    BUTTON_CLICK_QUAD,
    BUTTON_CLICK_QUINT,
} button_click_event_t;

// Sự kiện hold
typedef enum {
    BUTTON_HOLD_START   = 0x10,
    BUTTON_HOLD_TICK    = 0x11,
    BUTTON_HOLD_END     = 0x12,
} button_hold_event_t;

// Callback click
typedef void (*button_click_callback_t)(
    uint8_t button_id,
    button_click_event_t event,
    uint32_t click_count
);

// Callback hold
typedef void (*button_hold_callback_t)(
    uint8_t button_id,
    button_hold_event_t event,
    uint32_t hold_duration_ms
);

// Cấu hình nút
typedef struct {
    gpio_num_t gpio_num;
    bool active_level;                  // true = active high, false = active low
    uint16_t debounce_ms;
    uint16_t multi_click_timeout_ms;
    uint16_t long_press_threshold_ms;
    uint16_t hold_tick_interval_ms;
} button_config_t;

// API
esp_err_t button_init(const button_config_t *configs, uint8_t button_count);

esp_err_t button_register_click_callback(uint8_t button_id, button_click_callback_t cb);
esp_err_t button_register_hold_callback (uint8_t button_id, button_hold_callback_t cb);

esp_err_t button_unregister_click_callback(uint8_t button_id);
esp_err_t button_unregister_hold_callback (uint8_t button_id);

void button_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // BUTTON_H