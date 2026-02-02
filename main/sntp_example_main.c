/* LwIP SNTP example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "esp_netif_sntp.h"
#include "lwip/ip_addr.h"
#include "esp_sntp.h"
#include "sntp_manager.h"

static const char *TAG = "example";

void app_main(void)
{
    ESP_ERROR_CHECK( nvs_flash_init() );
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK( esp_event_loop_create_default() );
    ESP_ERROR_CHECK(example_connect());
    // Chỉ cần gọi 1 dòng này
    ESP_ERROR_CHECK( sntp_manager_init("asia.pool.ntp.org", 20, true) );
    // hoặc: sntp_manager_init("time.google.com", 7200, false);

    // Sau đó ở bất kỳ đâu cũng có thể lấy giờ dễ dàng
    while (1) {
        if (sntp_manager_is_time_synced()) {
            struct tm timeinfo;
            time_t now = sntp_manager_get_time(&timeinfo);
            // dùng timeinfo hoặc now ...
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static void obtain_time(void)
{
    ESP_ERROR_CHECK( nvs_flash_init() );
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK( esp_event_loop_create_default() );
    ESP_ERROR_CHECK(example_connect());
}
