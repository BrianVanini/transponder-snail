#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
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
#include "call.h"
#include "audio.h"

// Wiring: inputs, ESP-NOW radio and DFPlayer feed events into the call state machine (call.c).
#define PTT_GPIO      GPIO_NUM_32
#define HOOK_GPIO     GPIO_NUM_33
#define POLL_MS       10
#define DEBOUNCE_MS   30
#define DEBUG_PINGS   1             // 1 = ping every peer and print RSSI (range test)
#define PING_MS       5000
#define WIFI_CHANNEL  1
#define NAME_LEN      8
#define DFP_UART      UART_NUM_2
#define DFP_TX_GPIO   GPIO_NUM_17   // -> 1k resistor -> DFPlayer RX
#define DFP_RX_GPIO   GPIO_NUM_16   // <- DFPlayer TX
#define DFP_VOLUME    30            // 0-30

// Hardware test modes for the I2S mic and amp (docs/testing-mic-and-amp.md).
// When not 0, the board runs ONLY the test below: no radio, no call logic, no DFPlayer.
//   0 = normal snail firmware (default).
//   1 = BEEP: hold PTT to play a 1 kHz tone through the amp + top speaker.
//   2 = MIC METER: prints the mic level 5 times a second, plus wiring hints.
//   3 = LOOPBACK: hold PTT to hear the mic live through the speaker.
//       Expect a squeal (feedback) if the speaker points at the mic or the volume is high.
//       Keep them apart; set the volume with TEST_VOLUME.
#define AUDIO_TEST    0
#define TEST_VOLUME   AUDIO_DEFAULT_VOLUME   // amp volume in the test modes, 0-100 (%)

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

// Over-the-air message (types in call.h). Keep it small and fixed-size.
typedef struct __attribute__((packed)) {
    uint8_t type;            // msg_type_t
    char name[NAME_LEN];     // sender name, NUL-terminated
} snail_msg_t;

// Received message handed from the WiFi task to app_main via a queue.
typedef struct {
    uint8_t type;
    int from;                // SNAILS[] index, -1 if not in the table
    int rssi;
    char name[NAME_LEN];
} rx_event_t;

static QueueHandle_t rx_queue;

// Table index for a MAC, or -1 if unknown.
static int snail_index(const uint8_t *mac)
{
    for (int i = 0; i < NUM_SNAILS; i++) {
        if (memcmp(SNAILS[i].mac, mac, 6) == 0) return i;
    }
    return -1;
}

// Runs in the WiFi task: copy what we need and get out.
static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len < (int)sizeof(snail_msg_t)) return;
    const snail_msg_t *msg = (const snail_msg_t *)data;
    rx_event_t ev = { .type = msg->type, .from = snail_index(info->src_addr), .rssi = info->rx_ctrl->rssi };
    if (ev.from >= 0) {
        strncpy(ev.name, SNAILS[ev.from].name, NAME_LEN - 1);
    } else {
        memcpy(ev.name, msg->name, NAME_LEN);
        ev.name[NAME_LEN - 1] = '\0';
    }
    xQueueSend(rx_queue, &ev, 0);  // drop if full rather than block the WiFi task
}

// Only failures are printed. Successful sends stay quiet, otherwise the keep-alive
// (MSG_ALIVE every second while connected) would flood the log with "delivered" lines.
// For range checks, look at the "ping from X, RSSI ..." lines on the receiving board.
static void on_send(const esp_now_send_info_t *info, esp_now_send_status_t status)
{
    if (status == ESP_NOW_SEND_SUCCESS) return;
    int i = snail_index(info->des_addr);
    printf("  -> %s: FAILED\n", i >= 0 ? SNAILS[i].name : "?");
}

static void send_msg(int peer, msg_type_t type)
{
    snail_msg_t msg = { .type = type };
    strncpy(msg.name, SNAILS[self_idx].name, NAME_LEN - 1);
    esp_err_t err = esp_now_send(SNAILS[peer].mac, (const uint8_t *)&msg, sizeof(msg));
    if (err != ESP_OK) printf("esp_now_send to %s: %s\n", SNAILS[peer].name, esp_err_to_name(err));
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

// Inputs with internal pull-ups, so no external resistors are needed.
static void inputs_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PTT_GPIO) | (1ULL << HOOK_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));  // let pull-ups settle
}

// Pressed button / closed reed switch pulls the pin LOW.
static void print_hook(int level) { printf("Hook: %s\n", level ? "lifted" : "down"); }

// Callbacks for the call state machine.
static void play_track(uint16_t n)
{
    printf("DFPlayer: play %04d\n", n);
    dfplayer_play_mp3(n);
}

static void stop_track(void)
{
    printf("DFPlayer: stop\n");
    dfplayer_stop();
}

static const char *peer_name(int i) { return SNAILS[i].name; }

static const call_io_t CALL_IO = {
    .send = send_msg,
    .play = play_track,
    .stop = stop_track,
    .name = peer_name,
};

// ---------------------------------------------------------------------------
// AUDIO_TEST modes (see the #define at the top). Runs forever, never returns.

#define TEST_BLOCK    (AUDIO_SAMPLE_RATE / 100)   // 10 ms of samples = one POLL_MS step
#define METER_BLOCKS  20                          // 20 x 10 ms = print every 200 ms

static void print_pins(void)
{
    printf("Audio test mode %d\n", AUDIO_TEST);
    printf("  Mic (I2S0): BCLK=IO%d  WS/LRCL=IO%d  DOUT->IO%d   3V->3V3 (NOT 5V), SEL unconnected\n",
           MIC_BCLK_GPIO, MIC_WS_GPIO, MIC_DOUT_GPIO);
    printf("  Amp (I2S1): BCLK=IO%d  LRC=IO%d  DIN=IO%d   Vin->5V, GAIN/SD unconnected\n",
           AMP_BCLK_GPIO, AMP_LRC_GPIO, AMP_DIN_GPIO);
    printf("  PTT: IO%d   Sample rate %d Hz   Volume %d%%\n", PTT_GPIO, AUDIO_SAMPLE_RATE, TEST_VOLUME);
}

// Mode 1: 1 kHz sine while PTT is held. 16000 / 1000 = 16 samples per wave.
static void test_beep(debounced_t *ptt)
{
    int16_t wave[16];
    for (int i = 0; i < 16; i++) {
        wave[i] = (int16_t)(16000 * sinf(2.0f * (float)M_PI * i / 16));   // about half of full scale
    }
    int16_t block[TEST_BLOCK];
    for (int i = 0; i < TEST_BLOCK; i++) block[i] = wave[i % 16];   // 160 is a multiple of 16

    printf("Hold PTT to beep.\n");
    while (1) {
        if (debounce_update(ptt)) printf(ptt->stable == 0 ? "Beep on\n" : "Beep off\n");
        if (ptt->stable == 0) {
            audio_amp_write(block, TEST_BLOCK);    // takes ~10 ms, so no extra delay
        } else {
            vTaskDelay(pdMS_TO_TICKS(POLL_MS));    // amp falls silent on its own
        }
    }
}

// Mode 2: print peak/RMS every 200 ms, plus a bar and wiring hints.
static void test_mic_meter(void)
{
    static int16_t block[TEST_BLOCK];
    printf("Mic meter: talk or tap near the mic's hole (bottom of the board).\n");
    while (1) {
        int peak = 0, bad = 0;
        int64_t sum_sq = 0;
        size_t total = 0;
        mic_status_t last_bad = MIC_OK;
        for (int b = 0; b < METER_BLOCKS; b++) {
            size_t n = audio_mic_read(block, TEST_BLOCK);
            mic_status_t st = audio_mic_status();
            if (st != MIC_OK) { bad++; last_bad = st; }
            for (size_t i = 0; i < n; i++) {
                int v = block[i] < 0 ? -block[i] : block[i];
                if (v > peak) peak = v;
                sum_sq += (int64_t)block[i] * block[i];
            }
            total += n;
        }
        int rms = total ? (int)sqrtf((float)sum_sq / total) : 0;

        // Bar: one '#' per doubling of the peak (log scale), 0..15 marks.
        char bar[16];
        int marks = 0;
        for (int v = peak; v > 0; v >>= 1) marks++;
        if (marks > 15) marks = 15;
        for (int i = 0; i < 15; i++) bar[i] = i < marks ? '#' : ' ';
        bar[15] = '\0';
        printf("mic peak %5d rms %5d |%s|", peak, rms, bar);

        if (bad == METER_BLOCKS) {
            if (last_bad == MIC_NO_DATA)    printf("  mic: no data (check DOUT->IO35, 3V, GND, BCLK, WS)");
            if (last_bad == MIC_STUCK)      printf("  mic: stuck line (DOUT stuck high/low? check DOUT->IO35, GND)");
            if (last_bad == MIC_WRONG_SLOT) printf("  mic: data in the other slot (set MIC_SLOT in audio.c, or tie SEL to GND)");
        } else if (bad > 0) {
            printf("  dropouts %d/%d (loose contact? press the mic board down)", bad, METER_BLOCKS);
        }
        printf("\n");
    }
}

// Mode 3: mic -> amp while PTT is held. Mic reads and amp writes both run at the
// sample rate, so each loop pass takes ~10 ms.
static void test_loopback(debounced_t *ptt)
{
    static int16_t block[TEST_BLOCK];
    printf("Hold PTT to hear the mic. Squeal = feedback: move the speaker away, lower TEST_VOLUME.\n");
    while (1) {
        if (debounce_update(ptt)) printf(ptt->stable == 0 ? "Loopback on\n" : "Loopback off\n");
        if (ptt->stable == 0) {
            size_t n = audio_mic_read(block, TEST_BLOCK);
            audio_amp_write(block, n);
            if (n == 0) vTaskDelay(pdMS_TO_TICKS(POLL_MS));   // mic missing: don't spin
        } else {
            vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        }
    }
}

static void audio_test(void)
{
    print_pins();
    inputs_init();
    debounced_t ptt;
    debounce_init(&ptt, PTT_GPIO);

    if (AUDIO_TEST == 1 || AUDIO_TEST == 3) {
        audio_amp_init();
        audio_amp_set_volume(TEST_VOLUME);
    }
    if (AUDIO_TEST == 2 || AUDIO_TEST == 3) audio_mic_init();

    if (AUDIO_TEST == 1) test_beep(&ptt);
    if (AUDIO_TEST == 2) test_mic_meter();
    if (AUDIO_TEST == 3) test_loopback(&ptt);
    while (1) {
        printf("Unknown AUDIO_TEST %d (use 0-3)\n", AUDIO_TEST);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main(void)
{
    if (AUDIO_TEST != 0) audio_test();   // never returns

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

    inputs_init();

    debounced_t ptt, hook;
    debounce_init(&ptt, PTT_GPIO);
    debounce_init(&hook, HOOK_GPIO);
    print_hook(hook.stable);
    call_init(&CALL_IO, self_idx, NUM_SNAILS, hook.stable == 1);

    int ping_ms = 0;
    while (1) {
        if (debounce_update(&ptt)) call_ptt(ptt.stable == 0);  // LOW = pressed
        if (debounce_update(&hook)) {
            print_hook(hook.stable);
            call_hook(hook.stable == 1);                       // HIGH = lifted
        }

        ping_ms += POLL_MS;
        if (DEBUG_PINGS && ping_ms >= PING_MS) {
            ping_ms = 0;
            for (int i = 0; i < NUM_SNAILS; i++) {
                if (i != self_idx) send_msg(i, MSG_PING);
            }
        }

        rx_event_t ev;
        while (xQueueReceive(rx_queue, &ev, 0) == pdTRUE) {
            if (ev.type == MSG_PING) {
                if (DEBUG_PINGS) printf("ping from %s, RSSI %d dBm\n", ev.name, ev.rssi);
            } else if (ev.from < 0) {
                printf("Ignoring message %u from unknown snail \"%s\"\n", ev.type, ev.name);
            } else {
                call_msg(ev.from, (msg_type_t)ev.type);
            }
        }

        if (dfplayer_poll()) call_track_done();
        call_tick(POLL_MS);
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}
