#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_mac.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "nvs_flash.h"
#include "dfplayer.h"

// ESP-NOW link test: PTT sends RING to every other snail, pings every 2 s.
// DFPlayer plays /mp3/0001.mp3 (the ring) on PTT press and on incoming RING.
#define PTT_GPIO      GPIO_NUM_32
#define HOOK_GPIO     GPIO_NUM_33
#define POLL_MS       10
#define DEBOUNCE_MS   30
#define PING_MS       2000
#define WIFI_CHANNEL  1
#define NAME_LEN      8
#define DFP_UART      UART_NUM_2
#define DFP_TX_GPIO   GPIO_NUM_17   // -> 1k resistor -> DFPlayer RX
#define DFP_RX_GPIO   GPIO_NUM_16   // <- DFPlayer TX
#define DFP_VOLUME    20            // 0-30
#define RING_TRACK    1

// Known snails (WiFi STA MACs). To add a snail, add a row here.
typedef struct {
    const char *name;
    uint8_t mac[6];
} snail_t;

static const snail_t SNAILS[] = {
    { "A", { 0xC8, 0x85, 0x41, 0x2A, 0x0B, 0x98 } },
    { "B", { 0x20, 0x50, 0x0D, 0xE9, 0x99, 0xBC } },
};
#define NUM_SNAILS ((int)(sizeof(SNAILS) / sizeof(SNAILS[0])))

static int self_idx = -1;

// Over-the-air message. Keep it small and fixed-size.
typedef enum {
    MSG_RING = 1,
    MSG_PING,
    MSG_ANSWER,   // reserved for later
    MSG_HANGUP,   // reserved for later
} msg_type_t;

typedef struct __attribute__((packed)) {
    uint8_t type;            // msg_type_t
    char name[NAME_LEN];     // sender name, NUL-terminated
} snail_msg_t;

// Received message handed from the WiFi task to app_main via a queue.
typedef struct {
    uint8_t type;
    int rssi;
    char name[NAME_LEN];
} rx_event_t;

static QueueHandle_t rx_queue;

// Name for a MAC from the table, or NULL if unknown.
static const char *snail_name(const uint8_t *mac)
{
    for (int i = 0; i < NUM_SNAILS; i++) {
        if (memcmp(SNAILS[i].mac, mac, 6) == 0) return SNAILS[i].name;
    }
    return NULL;
}

// Runs in the WiFi task: copy what we need and get out.
static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len < (int)sizeof(snail_msg_t)) return;
    const snail_msg_t *msg = (const snail_msg_t *)data;
    rx_event_t ev = { .type = msg->type, .rssi = info->rx_ctrl->rssi };
    const char *name = snail_name(info->src_addr);
    if (name) {
        strncpy(ev.name, name, NAME_LEN - 1);
    } else {
        memcpy(ev.name, msg->name, NAME_LEN);
        ev.name[NAME_LEN - 1] = '\0';
    }
    xQueueSend(rx_queue, &ev, 0);  // drop if full rather than block the WiFi task
}

static void on_send(const esp_now_send_info_t *info, esp_now_send_status_t status)
{
    const char *name = snail_name(info->des_addr);
    printf("  -> %s: %s\n", name ? name : "?",
           status == ESP_NOW_SEND_SUCCESS ? "delivered" : "FAILED");
}

static void send_to_peers(msg_type_t type)
{
    snail_msg_t msg = { .type = type };
    strncpy(msg.name, SNAILS[self_idx].name, NAME_LEN - 1);
    for (int i = 0; i < NUM_SNAILS; i++) {
        if (i == self_idx) continue;
        esp_err_t err = esp_now_send(SNAILS[i].mac, (const uint8_t *)&msg, sizeof(msg));
        if (err != ESP_OK) printf("esp_now_send to %s: %s\n", SNAILS[i].name, esp_err_to_name(err));
    }
}

static void radio_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // WiFi in station mode, not connected to any router; ESP-NOW just needs the radio on.
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE));

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_send));

    // Every other snail in the table is a peer.
    for (int i = 0; i < NUM_SNAILS; i++) {
        if (i == self_idx) continue;
        esp_now_peer_info_t peer = {
            .channel = WIFI_CHANNEL,
            .ifidx = WIFI_IF_STA,
            .encrypt = false,
        };
        memcpy(peer.peer_addr, SNAILS[i].mac, 6);
        ESP_ERROR_CHECK(esp_now_add_peer(&peer));
    }
}

// One debounced input: 'stable' is the accepted level, 'raw' the last reading.
typedef struct {
    gpio_num_t pin;
    int stable;
    int raw;
    int stable_ms;
} debounced_t;

static void debounce_init(debounced_t *d, gpio_num_t pin)
{
    d->pin = pin;
    d->stable = d->raw = gpio_get_level(pin);
    d->stable_ms = 0;
}

// Returns true when the accepted level changes.
static bool debounce_update(debounced_t *d)
{
    int level = gpio_get_level(d->pin);
    if (level != d->raw) {
        d->raw = level;
        d->stable_ms = 0;
        return false;
    }
    if (d->stable_ms < DEBOUNCE_MS) {
        d->stable_ms += POLL_MS;
        if (d->stable_ms >= DEBOUNCE_MS && d->raw != d->stable) {
            d->stable = d->raw;
            return true;
        }
    }
    return false;
}

// Pressed button / closed reed switch pulls the pin LOW.
static void print_hook(int level) { printf("Hook: %s\n", level ? "lifted" : "down"); }

static void play_ring(void)
{
    printf("DFPlayer: play %04d\n", RING_TRACK);
    dfplayer_play_mp3(RING_TRACK);
}

static void print_peers(void)
{
    printf("Calling ");
    bool first = true;
    for (int i = 0; i < NUM_SNAILS; i++) {
        if (i == self_idx) continue;
        printf("%s%s", first ? "" : ", ", SNAILS[i].name);
        first = false;
    }
    printf("...\n");
}

void app_main(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    printf("Snail MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    for (int i = 0; i < NUM_SNAILS; i++) {
        if (memcmp(SNAILS[i].mac, mac, 6) == 0) self_idx = i;
    }
    if (self_idx < 0) {
        while (1) {
            printf("!!! UNKNOWN SNAIL %02X:%02X:%02X:%02X:%02X:%02X - add it to SNAILS[] in main.c !!!\n",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
    printf("I am Snail %s\n", SNAILS[self_idx].name);

    rx_queue = xQueueCreate(16, sizeof(rx_event_t));
    radio_init();
    dfplayer_init(DFP_UART, DFP_TX_GPIO, DFP_RX_GPIO);
    dfplayer_set_volume(DFP_VOLUME);

    // Inputs with internal pull-ups, so no external resistors are needed.
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PTT_GPIO) | (1ULL << HOOK_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));  // let pull-ups settle

    debounced_t ptt, hook;
    debounce_init(&ptt, PTT_GPIO);
    debounce_init(&hook, HOOK_GPIO);
    print_hook(hook.stable);

    int ping_ms = 0;
    while (1) {
        if (debounce_update(&ptt) && ptt.stable == 0) {  // falling edge = press
            print_peers();
            send_to_peers(MSG_RING);
            play_ring();
        }
        if (debounce_update(&hook)) print_hook(hook.stable);

        ping_ms += POLL_MS;
        if (ping_ms >= PING_MS) {
            ping_ms = 0;
            send_to_peers(MSG_PING);
        }

        rx_event_t ev;
        while (xQueueReceive(rx_queue, &ev, 0) == pdTRUE) {
            if (ev.type == MSG_RING) {
                printf("*** Incoming call from %s! (RSSI %d dBm) ***\n", ev.name, ev.rssi);
                play_ring();
            } else if (ev.type == MSG_PING) {
                printf("ping from %s, RSSI %d dBm\n", ev.name, ev.rssi);
            }
        }

        dfplayer_poll();
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}
