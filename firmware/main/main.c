#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Step 1 toolchain check: confirms build, flash, and serial monitor all work.
void app_main(void)
{
    int count = 0;
    while (1) {
        printf("Snail alive, count = %d\n", count++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
