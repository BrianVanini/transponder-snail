#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "dfplayer.h"

#define DFP_BAUD       9600
#define DFP_BOOT_MS    1500   // DFPlayer ignores commands while it reads the card
#define DFP_FRAME_LEN  10

#define CMD_VOLUME     0x06
#define CMD_PLAY_MP3   0x12
#define CMD_STOP       0x16

static uart_port_t dfp_port;

// Frame: 7E FF 06 CMD FEEDBACK PARAM_H PARAM_L CHK_H CHK_L EF,
// checksum = 0 - (sum of bytes 1..6).
static void send_cmd(uint8_t cmd, uint16_t param)
{
    uint8_t f[DFP_FRAME_LEN] = { 0x7E, 0xFF, 0x06, cmd, 0x00, param >> 8, param & 0xFF, 0, 0, 0xEF };
    uint16_t sum = 0;
    for (int i = 1; i <= 6; i++) sum += f[i];
    uint16_t chk = (uint16_t)(0 - sum);
    f[7] = chk >> 8;
    f[8] = chk & 0xFF;
    uart_write_bytes(dfp_port, f, sizeof(f));
}

void dfplayer_init(uart_port_t port, gpio_num_t tx_pin, gpio_num_t rx_pin)
{
    dfp_port = port;
    uart_config_t cfg = {
        .baud_rate = DFP_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(port, 256, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(port, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(port, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    vTaskDelay(pdMS_TO_TICKS(DFP_BOOT_MS));
}

void dfplayer_set_volume(uint8_t vol) { send_cmd(CMD_VOLUME, vol > 30 ? 30 : vol); }
void dfplayer_play_mp3(uint16_t n) { send_cmd(CMD_PLAY_MP3, n); }
void dfplayer_stop(void) { send_cmd(CMD_STOP, 0); }

void dfplayer_poll(void)
{
    static uint8_t buf[DFP_FRAME_LEN];
    static int pos = 0;
    uint8_t b;
    while (uart_read_bytes(dfp_port, &b, 1, 0) == 1) {
        if (pos == 0 && b != 0x7E) continue;  // wait for a frame start
        buf[pos++] = b;
        if (pos == DFP_FRAME_LEN) {
            if (buf[9] == 0xEF) {
                printf("DFPlayer: got 0x%02X (param %u)\n", buf[3], (buf[5] << 8) | buf[6]);
            }
            pos = 0;
        }
    }
}
