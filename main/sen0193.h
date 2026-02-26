#ifndef SEN0193_H
#define SEN0193_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ────────────────────────────────────────────────
// CẤU HÌNH - CÁC THAM SỐ DỄ CHỈNH SỬA
// ────────────────────────────────────────────────

// Số mẫu ADC để lấy trung bình khi đọc giá trị (giảm nhiễu)
#define SEN0193_READING_SAMPLES                 8

// Delay giữa các mẫu khi đọc trung bình (ms)
#define SEN0193_SAMPLE_DELAY_MS                 10

// Số mẫu để tính trung bình trong quá trình calibration
#define SEN0193_CALIBRATION_SAMPLES             20

// Delay giữa các mẫu khi calibration (ms)
#define SEN0193_CALIBRATION_SAMPLE_DELAY_MS     50

// Ngưỡng kiểm tra calibration có hợp lệ không
#define SEN0193_MIN_VALID_WET_VALUE             500
#define SEN0193_MIN_VALID_DRY_VALUE             1000
#define SEN0193_MIN_DRY_WET_DIFFERENCE          800

// Namespace và key trong NVS
#define SEN0193_NVS_NAMESPACE                   "sen0193"
#define SEN0193_NVS_KEY_DRY                     "dry"
#define SEN0193_NVS_KEY_WET                     "wet"

// ────────────────────────────────────────────────

typedef enum {
    SEN0193_STATE_UNINITIALIZED = 0,
    SEN0193_STATE_INITIALIZING,
    SEN0193_STATE_READY_NOT_CALIBRATED,
    SEN0193_STATE_CALIBRATING_DRY,
    SEN0193_STATE_CALIBRATING_WET,
    SEN0193_STATE_READY_CALIBRATED,
    SEN0193_STATE_ERROR
} sen0193_state_t;

typedef struct {
    adc1_channel_t channel;
    adc_atten_t atten;
    esp_adc_cal_characteristics_t *adc_chars;
    
    // Calibration values
    uint32_t dry_value;
    uint32_t wet_value;
    bool has_valid_calibration;

    // Runtime state
    sen0193_state_t state;
    
    // Latest reading (được task cập nhật)
    uint32_t last_raw;
    uint32_t last_voltage_mv;
    int last_moisture_percent;

    // Task control
    bool task_running;
    TickType_t read_interval_ticks;     // ticks (không phải ms)
} sen0193_handle_t;

// Khởi tạo cảm biến + tạo task đọc nền
esp_err_t sen0193_start(sen0193_handle_t *handle,
                        adc1_channel_t channel,
                        adc_atten_t atten,
                        uint32_t read_interval_ms);

// Dừng task và giải phóng tài nguyên
esp_err_t sen0193_stop(sen0193_handle_t *handle);

// Lấy trạng thái hiện tại
sen0193_state_t sen0193_get_state(const sen0193_handle_t *handle);

// Chuỗi mô tả trạng thái (dùng để log hoặc hiển thị)
const char* sen0193_state_to_string(sen0193_state_t state);

// Bắt đầu calibration khô (đặt cảm biến trong không khí)
esp_err_t sen0193_start_calibrate_dry(sen0193_handle_t *handle);

// Bắt đầu calibration ướt (ngâm nước)
esp_err_t sen0193_start_calibrate_wet(sen0193_handle_t *handle);

// Kết thúc calibration → tính trung bình, kiểm tra, lưu NVS
esp_err_t sen0193_finish_calibration(sen0193_handle_t *handle);

// Lấy giá trị đọc mới nhất (an toàn khi dùng đa nhiệm)
void sen0193_get_latest_values(const sen0193_handle_t *handle,
                               uint32_t *raw_out,
                               uint32_t *voltage_mv_out,
                               int *moisture_percent_out);

// Xóa calibration đã lưu trong NVS
esp_err_t sen0193_clear_calibration(sen0193_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* SEN0193_H */