#include "button.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "button_poll";

typedef struct {
    button_config_t config;
    button_click_callback_t click_cb;
    button_hold_callback_t  hold_cb;

    // Trạng thái
    bool last_raw_state;           // trạng thái thô lần trước
    bool debounced_state;          // trạng thái đã debounce
    int64_t last_debounce_time;    // thời điểm thay đổi trạng thái thô cuối
    int64_t press_start_time;
    uint8_t click_count;
    bool waiting_multi_click;

    // Hold
    bool is_holding;
    int64_t last_tick_time;
    bool hold_start_sent;
    bool hold_end_sent;
} button_instance_t;

static button_instance_t buttons[BUTTON_MAX_COUNT];
static uint8_t button_count = 0;
static TaskHandle_t polling_task_handle = NULL;

// Task quét định kỳ tất cả nút
static void button_polling_task(void *arg)
{
    int64_t now;

    while (1) {
        now = esp_timer_get_time() / 1000LL;

        for (uint8_t i = 0; i < button_count; i++) {
            button_instance_t *btn = &buttons[i];

            // Đọc GPIO (polling)
            bool raw_state = (gpio_get_level(btn->config.gpio_num) == btn->config.active_level);

            // Debounce logic
            if (raw_state != btn->last_raw_state) {
                btn->last_debounce_time = now;
                btn->last_raw_state = raw_state;
            }

            bool stable = (now - btn->last_debounce_time) >= btn->config.debounce_ms;

            if (stable && (raw_state != btn->debounced_state)) {
                btn->debounced_state = raw_state;

                if (raw_state) {  // Nhấn xuống (Pressed)
                    btn->press_start_time    = now;
                    btn->waiting_multi_click = false;
                    btn->is_holding          = false;
                    btn->hold_start_sent     = false;
                    btn->hold_end_sent       = false;
                    btn->last_tick_time      = now;
                } else {  // Nhả ra (Released)
                    uint32_t duration_ms = (uint32_t)(now - btn->press_start_time);

                    if (btn->is_holding) {
                        // Kết thúc hold
                        if (btn->hold_cb && !btn->hold_end_sent) {
                            btn->hold_cb(i, BUTTON_HOLD_END, duration_ms);
                            btn->click_count         = 0;
                            btn->hold_end_sent = true;
                        }
                    } else {
                        // Là click → bắt đầu chờ multi-click
                        btn->click_count++;
                        btn->waiting_multi_click = true;
                        btn->last_debounce_time = now;  // dùng lại để timeout multi-click
                        ESP_LOGI(TAG, "Button %d: Detected click #%d, waiting for multi-click...", i, btn->click_count);
                    }
                }
            }

            // 1. Timeout multi-click (chỉ khi đã nhả)
            if (!btn->debounced_state && btn->waiting_multi_click) {
                if ((now - btn->last_debounce_time) >= btn->config.multi_click_timeout_ms) {
                    if (btn->click_count >= 1 && btn->click_count <= 5) {
                        button_click_event_t evt = BUTTON_CLICK_SINGLE;
                        switch (btn->click_count) {
                            case 2: evt = BUTTON_CLICK_DOUBLE; break;
                            case 3: evt = BUTTON_CLICK_TRIPLE; break;
                            case 4: evt = BUTTON_CLICK_QUAD;   break;
                            case 5: evt = BUTTON_CLICK_QUINT;  break;
                            default: break;
                        }
                        if (btn->click_cb) {
                            btn->click_cb(i, evt, btn->click_count);
                        }
                    }
                    btn->waiting_multi_click = false;
                    btn->click_count = 0;
                }
            }

            // 2. Hold logic (chỉ khi đang nhấn)
            if (btn->debounced_state) {
                uint32_t hold_dur = (uint32_t)(now - btn->press_start_time);

                // Bắt đầu hold
                if (!btn->is_holding && hold_dur >= btn->config.long_press_threshold_ms) {
                    btn->is_holding = true;
                    btn->last_tick_time = now;

                    if (btn->hold_cb) {
                        btn->hold_cb(i, BUTTON_HOLD_START, hold_dur);
                    }
                    btn->hold_start_sent = true;
                }

                // Tick định kỳ
                if (btn->is_holding) {
                    if ((now - btn->last_tick_time) >= btn->config.hold_tick_interval_ms) {
                        if (btn->hold_cb) {
                            btn->hold_cb(i, BUTTON_HOLD_TICK, hold_dur);
                        }
                        btn->last_tick_time += btn->config.hold_tick_interval_ms;
                    }
                }
            } else {
                btn->is_holding = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLLING_INTERVAL_MS));
    }
}

esp_err_t button_init(const button_config_t *configs, uint8_t count)
{
    if (count == 0 || count > BUTTON_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    if (polling_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    button_count = count;

    for (uint8_t i = 0; i < count; i++) {
        buttons[i].config = configs[i];

        // Mặc định nếu không set
        if (buttons[i].config.debounce_ms == 0)
            buttons[i].config.debounce_ms = BUTTON_DEFAULT_DEBOUNCE_MS;
        if (buttons[i].config.multi_click_timeout_ms == 0)
            buttons[i].config.multi_click_timeout_ms = BUTTON_DEFAULT_CLICK_TIMEOUT_MS;
        if (buttons[i].config.long_press_threshold_ms == 0)
            buttons[i].config.long_press_threshold_ms = BUTTON_DEFAULT_LONG_PRESS_MS;
        if (buttons[i].config.hold_tick_interval_ms == 0)
            buttons[i].config.hold_tick_interval_ms = BUTTON_DEFAULT_HOLD_TICK_MS;

        buttons[i].click_cb = NULL;
        buttons[i].hold_cb  = NULL;

        buttons[i].last_raw_state     = false;
        buttons[i].debounced_state    = false;
        buttons[i].last_debounce_time = 0;
        buttons[i].press_start_time   = 0;
        buttons[i].click_count        = 0;
        buttons[i].waiting_multi_click= false;
        buttons[i].is_holding         = false;
        buttons[i].hold_start_sent    = false;
        buttons[i].hold_end_sent      = false;

        // Cấu hình GPIO - chỉ input, không interrupt
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << configs[i].gpio_num),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en   = configs[i].active_level ? GPIO_PULLUP_DISABLE : GPIO_PULLUP_ENABLE,
            .pull_down_en = configs[i].active_level ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE   // tắt interrupt
        };
        gpio_config(&io_conf);
    }

    // Tạo task polling
    BaseType_t ret = xTaskCreate(
        button_polling_task,
        "btn_poll_task",
        4096,
        NULL,
        5,
        &polling_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create polling task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Button polling initialized (%d buttons, scan every %d ms)", count, BUTTON_POLLING_INTERVAL_MS);
    return ESP_OK;
}

esp_err_t button_register_click_callback(uint8_t button_id, button_click_callback_t cb)
{
    if (button_id >= button_count) return ESP_ERR_INVALID_ARG;
    buttons[button_id].click_cb = cb;
    return ESP_OK;
}

esp_err_t button_register_hold_callback(uint8_t button_id, button_hold_callback_t cb)
{
    if (button_id >= button_count) return ESP_ERR_INVALID_ARG;
    buttons[button_id].hold_cb = cb;
    return ESP_OK;
}

esp_err_t button_unregister_click_callback(uint8_t button_id)
{
    if (button_id >= button_count) return ESP_ERR_INVALID_ARG;
    buttons[button_id].click_cb = NULL;
    return ESP_OK;
}

esp_err_t button_unregister_hold_callback(uint8_t button_id)
{
    if (button_id >= button_count) return ESP_ERR_INVALID_ARG;
    buttons[button_id].hold_cb = NULL;
    return ESP_OK;
}

void button_deinit(void)
{
    if (polling_task_handle) {
        vTaskDelete(polling_task_handle);
        polling_task_handle = NULL;
    }

    for (uint8_t i = 0; i < button_count; i++) {
        gpio_reset_pin(buttons[i].config.gpio_num);
    }

    button_count = 0;
    ESP_LOGI(TAG, "Button polling deinitialized");
}