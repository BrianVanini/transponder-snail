#pragma once

#include <stdint.h>
#include "driver/gpio.h"
#include "driver/uart.h"

// DFPlayer Mini (DFR0299) over UART, 9600 8N1.
// Waits ~1.5 s inside init so the DFPlayer can finish reading the microSD card.
void dfplayer_init(uart_port_t port, gpio_num_t tx_pin, gpio_num_t rx_pin);
void dfplayer_set_volume(uint8_t vol);  // 0-30
void dfplayer_play_mp3(uint16_t n);     // plays /mp3/000n.mp3
void dfplayer_stop(void);

// Log any frames the DFPlayer sent back (e.g. 0x3F card online, 0x40 error). Never blocks.
void dfplayer_poll(void);
