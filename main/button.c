#include "button.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "button";

typedef struct {
    button_config_t config;
    button_callback_t callback;
    
    // Trạng thái runtime
    bool last_state;
    bool debounced_state;
    int64_t last_change_time;
    int64_t press_start_time;
    uint8_t click_count;
    bool waiting_for_multi_click;
    bool long_press_sent;
} button_instance_t;

static button_instance_t buttons[BUTTON_MAX_COUNT];
static uint8_t button_count = 0;
static TaskHandle_t button_task_handle = NULL;
static QueueHandle_t button_event_queue = NULL;

static void IRAM_ATTR button_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    
    // Tìm button_id tương ứng
    for (uint8_t i = 0; i < button_count; i++) {
        if (buttons[i].config.gpio_num == gpio_num) {
            // Gửi thông báo vào queue (chỉ số button)
            xQueueSendFromISR(button_event_queue, &i, NULL);
            break;
        }
    }
}

static void button_task(void *pvParameters)
{
    uint8_t button_id;
    int64_t now;
    
    while (1) {
        if (xQueueReceive(button_event_queue, &button_id, portMAX_DELAY) == pdTRUE) {
            if (button_id >= button_count) continue;
            
            button_instance_t *btn = &buttons[button_id];
            now = esp_timer_get_time() / 1000;  // ms
            
            // Đọc trạng thái hiện tại
            bool current_state = gpio_get_level(btn->config.gpio_num) == btn->config.active_level;
            
            // Debounce
            if (current_state != btn->last_state) {
                btn->last_change_time = now;
                btn->last_state = current_state;
            }
            
            if ((now - btn->last_change_time) < btn->config.debounce_ms) {
                continue; // vẫn đang rung → bỏ qua
            }
            
            if (current_state != btn->debounced_state) {
                btn->debounced_state = current_state;
                
                if (current_state) {  // Nhấn xuống (Pressed)
                    btn->press_start_time = now;
                    btn->click_count = 0;
                    btn->waiting_for_multi_click = false;
                    btn->long_press_sent = false;
                    
                    // Gọi callback PRESSED (tuỳ chọn)
                    if (btn->callback) {
                        button_event_info_t info = {
                            .button_id = button_id,
                            .event = BUTTON_EVENT_PRESSED,
                            .click_count = 0,
                            .press_duration_ms = 0
                        };
                        btn->callback(&info);
                    }
                } else {  // Nhả ra (Released)
                    uint32_t duration = now - btn->press_start_time;
                    
                    // Gọi callback RELEASED (tuỳ chọn)
                    if (btn->callback) {
                        button_event_info_t info = {
                            .button_id = button_id,
                            .event = BUTTON_EVENT_RELEASED,
                            .click_count = 0,
                            .press_duration_ms = duration
                        };
                        btn->callback(&info);
                    }
                    
                    // Xử lý logic click / long press
                    if (duration >= btn->config.long_press_threshold_ms) {
                        // Long press
                        if (!btn->long_press_sent) {
                            if (btn->callback) {
                                button_event_info_t info = {
                                    .button_id = button_id,
                                    .event = BUTTON_EVENT_LONG_PRESS,
                                    .click_count = 0,
                                    .press_duration_ms = duration
                                };
                                btn->callback(&info);
                            }
                            btn->long_press_sent = true;
                        }
                    } else {
                        // Là một lần click → chờ xem có multi-click không
                        btn->click_count++;
                        btn->waiting_for_multi_click = true;
                        btn->last_change_time = now;  // dùng để timeout
                    }
                }
            }
        }
        
        // Kiểm tra timeout multi-click cho tất cả nút
        now = esp_timer_get_time() / 1000;
        for (uint8_t i = 0; i < button_count; i++) {
            button_instance_t *btn = &buttons[i];
            if (btn->waiting_for_multi_click) {
                if ((now - btn->last_change_time) >= btn->config.multi_click_timeout_ms) {
                    // Hết thời gian chờ → phát sự kiện multi-click
                    if (btn->click_count >= 1 && btn->click_count <= 5) {
                        button_event_t evt;
                        switch (btn->click_count) {
                            case 1: evt = BUTTON_EVENT_SINGLE_CLICK; break;
                            case 2: evt = BUTTON_EVENT_DOUBLE_CLICK; break;
                            case 3: evt = BUTTON_EVENT_TRIPLE_CLICK; break;
                            case 4: evt = BUTTON_EVENT_QUAD_CLICK; break;
                            case 5: evt = BUTTON_EVENT_QUINT_CLICK; break;
                            default: evt = BUTTON_EVENT_SINGLE_CLICK; break;
                        }
                        
                        if (btn->callback) {
                            button_event_info_t info = {
                                .button_id = i,
                                .event = evt,
                                .click_count = btn->click_count,
                                .press_duration_ms = 0
                            };
                            btn->callback(&info);
                        }
                    }
                    btn->waiting_for_multi_click = false;
                    btn->click_count = 0;
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t button_init(const button_config_t *configs, uint8_t count)
{
    if (count == 0 || count > BUTTON_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (button_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Cài đặt ISR service cho GPIO (chỉ cần gọi 1 lần)
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK) {
        if (err == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "GPIO ISR service already installed, continuing...");
        } else {
            ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(err));
            return err;
        }
    }

    button_count = count;
    button_event_queue = xQueueCreate(16, sizeof(uint8_t));
    if (button_event_queue == NULL) {
        return ESP_FAIL;
    }
    
    // Sao chép cấu hình và set mặc định nếu cần
    for (uint8_t i = 0; i < count; i++) {
        buttons[i].config = configs[i];
        
        // Giá trị mặc định nếu người dùng không set
        if (buttons[i].config.debounce_ms == 0)
            buttons[i].config.debounce_ms = BUTTON_DEFAULT_DEBOUNCE_MS;
        if (buttons[i].config.multi_click_timeout_ms == 0)
            buttons[i].config.multi_click_timeout_ms = BUTTON_DEFAULT_CLICK_TIMEOUT_MS;
        if (buttons[i].config.long_press_threshold_ms == 0)
            buttons[i].config.long_press_threshold_ms = BUTTON_DEFAULT_LONG_PRESS_MS;
        
        buttons[i].callback = NULL;
        buttons[i].last_state = false;
        buttons[i].debounced_state = false;
        buttons[i].last_change_time = 0;
        buttons[i].press_start_time = 0;
        buttons[i].click_count = 0;
        buttons[i].waiting_for_multi_click = false;
        buttons[i].long_press_sent = false;
        
        // Cấu hình GPIO
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << configs[i].gpio_num),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = (configs[i].active_level == false) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
            .pull_down_en = (configs[i].active_level == true) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_ANYEDGE
        };
        gpio_config(&io_conf);
        
        // Cài ISR
        gpio_isr_handler_add(configs[i].gpio_num, button_isr_handler, (void*)(uint32_t)configs[i].gpio_num);
    }
    
    // Tạo task xử lý
    xTaskCreate(button_task, "button_task", 4096, NULL, 5, &button_task_handle);
    
    ESP_LOGI(TAG, "Button driver initialized with %d buttons", count);
    return ESP_OK;
}

esp_err_t button_register_callback(uint8_t button_id, button_callback_t cb)
{
    if (button_id >= button_count) {
        return ESP_ERR_INVALID_ARG;
    }
    buttons[button_id].callback = cb;
    return ESP_OK;
}

esp_err_t button_unregister_callback(uint8_t button_id)
{
    if (button_id >= button_count) {
        return ESP_ERR_INVALID_ARG;
    }
    buttons[button_id].callback = NULL;
    return ESP_OK;
}

void button_deinit(void)
{
    if (button_task_handle) {
        vTaskDelete(button_task_handle);
        button_task_handle = NULL;
    }
    if (button_event_queue) {
        vQueueDelete(button_event_queue);
        button_event_queue = NULL;
    }
    
    for (uint8_t i = 0; i < button_count; i++) {
        gpio_isr_handler_remove(buttons[i].config.gpio_num);
        gpio_reset_pin(buttons[i].config.gpio_num);
    }
    
    button_count = 0;
    ESP_LOGI(TAG, "Button driver deinitialized");
}