#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_mac.h"

// Step 2 GPIO test: PTT button and reed switch (hook), both wired to GND.
#define PTT_GPIO      GPIO_NUM_32
#define HOOK_GPIO     GPIO_NUM_33
#define POLL_MS       10
#define DEBOUNCE_MS   30

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
static void print_ptt(int level)  { printf("PTT: %s\n", level ? "released" : "pressed"); }
static void print_hook(int level) { printf("Hook: %s\n", level ? "lifted" : "down"); }

void app_main(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    printf("Snail MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

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
    print_ptt(ptt.stable);
    print_hook(hook.stable);

    while (1) {
        if (debounce_update(&ptt))  print_ptt(ptt.stable);
        if (debounce_update(&hook)) print_hook(hook.stable);
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}
