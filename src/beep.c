#include <stdbool.h>
#include <stdint.h>
#include "stc15.h"
#include "hwconfig.h"
#include "timer0.h"

static uint8_t beep_timer = 1 * TMO_100MS;

void beep_start(uint8_t tmo)
{
    set_timer(&beep_timer, tmo);
}

void beep_handle(bool enabled)
{
    if (timer_elapsed(&beep_timer)) {
        BUZZER_OFF;
    } else {
        if(enabled) {
            BUZZER_ON;
        }
    }
}

