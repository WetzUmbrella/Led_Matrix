// LED wall panel firmware (same firmware on every panel)
//  - joins the router (USE_ROUTER 1) or the Pico's own LEDWALL network (USE_ROUTER 0)
//  - finds the Pico from the source address of its beacons (no hardcoded IP)
//  - Week 10 skew-rig build (T-B4-03): tile_net receives BEACON {session_id, seq, t_p},
//    the Time Sync Task corrects the clock, and display_engine pulses the test pin and
//    lights LED (n mod 64) on every 60 fps tick of the shared timeline
//  - the prototype's HELLO, layout beacon and 30 fps GIF path are no longer built

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "display_engine.h"
#include "tile_net.h"
#include "tile_tasks.h"
#include "timesync.h"

#define USE_ROUTER       1           // must match the Pico. 0 = join the Pico's own access point

#if USE_ROUTER
// Router credentials live in wifi_secrets.h (git-ignored, copy wifi_secrets.example.h)
#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#warning "wifi_secrets.h missing - using placeholder router credentials"
#define ROUTER_SSID      "CHANGE_ME"
#define ROUTER_PASS      "CHANGE_ME"
#endif
#define WIFI_SSID        ROUTER_SSID
#define WIFI_PASS        ROUTER_PASS
#else
#define WIFI_SSID        "LEDWALL"
#define WIFI_PASS        "ledwall123"
#endif

static const char *TAG = "panel";
static uint32_t my_id;
static uint8_t  my_board;            // from flash (set_board_number.sh), 0 = never set. Also my slot.

/* ---------- WiFi ---------- */
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "lost %s, retrying", WIFI_SSID);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "joined %s, IP " IPSTR, WIFI_SSID, IP2STR(&e->ip_info.ip));
    }
}

static void wifi_init(void)
{
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        r = nvs_flash_init();
    }
    ESP_ERROR_CHECK(r);

    // Board number written once by set_board_number.sh. It's this panel's router name (ESP-<n>)
    // and its fixed slot in the wall. Stored in NVS, so normal reflashes never touch it.
    static char hostname[16];
    nvs_handle_t nvs;
    if (nvs_open("board", NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u8(nvs, "number", &my_board);
        nvs_close(nvs);
    }
    if (my_board) {
        snprintf(hostname, sizeof(hostname), "ESP-%u", my_board);
    } else {
        snprintf(hostname, sizeof(hostname), "ESP-%08" PRIx32, my_id);
        ESP_LOGW(TAG, "no board number set, the Pico won't give this panel a slot. "
                      "Run ./set_board_number.sh <n>");
    }
    ESP_LOGI(TAG, "I am %s", hostname);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(sta, hostname);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL);

    wifi_config_t wc = { 0 };
    memcpy(wc.sta.ssid, WIFI_SSID, strlen(WIFI_SSID));
    memcpy(wc.sta.password, WIFI_PASS, strlen(WIFI_PASS));
    wc.sta.threshold.authmode = WIFI_PASS[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;   // "" = open network
    wc.sta.pmf_cfg.capable = true;   // lets us join WPA2/WPA3 mixed-mode routers

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);   // no radio sleep, or sync jitters by 100ms+
}

void app_main(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    my_id = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) | ((uint32_t)mac[4] << 8) | mac[5];
    ESP_LOGI(TAG, "panel id %08" PRIx32, my_id);

    wifi_init();

    // Week 10 skew-rig tasks, at the tile_tasks.h priorities and cores (T-B5-04):
    // Network 12 and Time Sync 16 on core 0, Display Engine 20 on core 1.
    if (!tile_net_start()) {
        ESP_LOGE(TAG, "tile_net_start failed");
    }
    if (xTaskCreatePinnedToCore(timesync_task, TILE_TIMESYNC_NAME, TILE_TIMESYNC_STACK,
                                tile_net_timesync_queue(), TILE_TIMESYNC_PRIO, NULL,
                                TILE_TIMESYNC_CORE) != pdPASS) {
        ESP_LOGE(TAG, "timesync_task not created");
    }
    if (!display_engine_start()) {
        ESP_LOGE(TAG, "display_engine_start failed");
    }
    // T-B5-04 acceptance: CPU frequency, cycle-counter wrap and vTaskList().
    // xTaskCreatePinnedToCore() returns after the task exists, so all three
    // tasks above are already in the list.
    tile_tasks_log_boot();
    // app_main returns here; the tasks above own the tile.
}
