// LED wall panel firmware (same firmware on every panel)
//  - joins the router (USE_ROUTER 1) or the Pico's own LEDWALL network (USE_ROUTER 0)
//  - finds the Pico from the source address of its beacons (no hardcoded IP)
//  - says HELLO to the Pico every 0.3s so it knows this panel is alive
//  - reads the Pico's beacon: frame clock + layout (panel count, grid, who is in which slot)
//  - draws its own piece of the GIF, re-splitting automatically when panels join or leave
//  - flashes its slot number for 2s whenever the layout changes, so you know where to place it

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
#include "lwip/sockets.h"
#include "render.h"

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

#define SYNC_PORT        4210
#define HELLO_PORT       4211
#define SYNC_MAGIC       0x4C454457
#define HELLO_MAGIC      0x48454C4F
#define PROTO_VERSION    3           // must match the Pico. 3: HELLO carries the board number
#define FPS              30          // must match the Pico
#define MAX_NODES        8

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  version;
    uint32_t seq;
    uint8_t  cmd;
    uint32_t frame_no;
    uint8_t  layout_ver;
    uint8_t  count;
    uint8_t  cols;
    uint8_t  rows;
    uint32_t ids[MAX_NODES];
} beacon_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t id;
    uint8_t  board;
} hello_t;

static const char *TAG = "panel";
static uint32_t my_id;
static uint8_t  my_board;            // from flash (set_board_number.sh), 0 = never set. Also my slot.

// Shared between the receive task and the display loop
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t last_frame = 0;
static int64_t  last_rx_us = 0;
static bool     synced = false;
static int      my_slot = -1;        // -1 = Pico hasn't listed us yet
static int      cur_count = 0, cur_cols = 1, cur_rows = 1;
static int      cur_layout_ver = -1;
static volatile in_addr_t pico_addr = 0;   // learned from beacons, 0 = not heard yet

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

/* ---------- HELLO heartbeat to the Pico ---------- */
static void hello_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in to = {
        .sin_family = AF_INET,
        .sin_port = htons(HELLO_PORT),
    };

    hello_t h = { .magic = HELLO_MAGIC, .id = my_id, .board = my_board };
    while (1) {
        to.sin_addr.s_addr = pico_addr;   // follows the Pico if the router gives it a new IP
        if (to.sin_addr.s_addr)           // wait until the first beacon tells us where the Pico is
            sendto(sock, &h, sizeof(h), 0, (struct sockaddr *)&to, sizeof(to));
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

/* ---------- beacon receiver ---------- */
static void sync_rx_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(SYNC_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));

    uint32_t expected = 0, drops = 0;
    bool first = true;

    while (1) {
        beacon_t b;
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int n = recvfrom(sock, &b, sizeof(b), 0, (struct sockaddr *)&from, &from_len);
        if (n != sizeof(b) || b.magic != SYNC_MAGIC || b.version != PROTO_VERSION) continue;

        if (from.sin_addr.s_addr != pico_addr) {
            pico_addr = from.sin_addr.s_addr;
            ESP_LOGI(TAG, "Pico found at %s", inet_ntoa(from.sin_addr));
        }

        int64_t now = esp_timer_get_time();
        if (!first && b.seq > expected) drops += b.seq - expected;
        first = false;
        expected = b.seq + 1;

        int count = b.count > MAX_NODES ? MAX_NODES : b.count;
        int slot = -1;
        for (int i = 0; i < count; i++)
            if (b.ids[i] == my_id) { slot = i; break; }

        taskENTER_CRITICAL(&lock);
        last_frame = b.frame_no;
        last_rx_us = now;
        synced = true;
        my_slot = slot;
        cur_count = count;
        cur_cols = b.cols ? b.cols : 1;
        cur_rows = b.rows ? b.rows : 1;
        cur_layout_ver = b.layout_ver;
        taskEXIT_CRITICAL(&lock);

        if (b.seq % 50 == 0)
            ESP_LOGI(TAG, "beacon seq=%" PRIu32 " frame=%" PRIu32 " drops=%" PRIu32 " panels=%d",
                     b.seq, b.frame_no, drops, count);
    }
}

/* ---------- hand-off to the renderer (render.c) ---------- */
// The render task calls this every refresh to get a consistent copy of the sync state.
static void get_render_state(render_state_t *out)
{
    taskENTER_CRITICAL(&lock);
    out->synced      = synced;
    out->slot        = my_slot;
    out->count       = cur_count;
    out->cols        = cur_cols;
    out->rows        = cur_rows;
    out->layout_ver  = cur_layout_ver;
    out->frame_no    = last_frame;
    out->frame_rx_us = last_rx_us;
    taskEXIT_CRITICAL(&lock);
}

void app_main(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    my_id = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) | ((uint32_t)mac[4] << 8) | mac[5];
    ESP_LOGI(TAG, "panel id %08" PRIx32, my_id);

    ESP_ERROR_CHECK(render_start(get_render_state, FPS));

    wifi_init();
    xTaskCreate(sync_rx_task, "sync_rx", 4096, NULL, 5, NULL);
    xTaskCreate(hello_task, "hello", 3072, NULL, 4, NULL);
    // app_main returns here; the render task (render.c) now owns the display loop.
}
