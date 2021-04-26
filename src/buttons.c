#include <stdint.h>
#include "stc15.h"
#include "timer0.h"
#include "buttons.h"

// hardware configuration
#include "hwconfig.h"

static volatile __bit S1_LONG;
static volatile __bit S1_PRESSED;
static volatile __bit S2_LONG;
static volatile __bit S2_PRESSED;
static volatile __bit S3_LONG;
static volatile __bit S3_PRESSED;

/*
// macro expansion for MONITOR_S(1)
{
    uint8_t s = 1 - 1;
    debounce[s] = (debounce[s] << 1) | SW1 ;
    if (debounce[s] == 0) {
        S_PRESSED = 1;
        if (!S_LONG) {
            switchcount[s]++;
        }
    } else {
        if (S1_PRESSED) {
            if (!S1_LONG) {
                ev = EV_S1_SHORT;
            }
            S1_PRESSED = 0;
            S1_LONG = 0;
            switchcount[s] = 0;
        }
    }
    if (switchcount[s] > SW_CNTMAX) {
        S1_LONG = 1;
        switchcount[s] = 0;
        ev = EV_S1_LONG;
    }
}
*/


/* This stores the last event.
 * After reading this should be set to EV_NONE,
 * new events will not be processed until it is set to EV_NONE */
enum ButtonEvent event;

void buttons_read(void)
{
    static uint8_t debounce[NUM_SW];      // switch debounce buffer
    static uint8_t switchcount[NUM_SW];
#define SW_CNTMAX (80*1)	//long push

    static uint8_t t = 0;
    /* Run only every 10ms */
    if(t != (time_now & TICK_10MS))
        return;
    t ^= 1;

    enum Event ev = EV_NONE;
    // Check SW status and chattering control
#define MONITOR_S(n) \
    { \
        uint8_t s = n - 1; \
        /* read switch positions into sliding 8-bit window */ \
        debounce[s] = (debounce[s] << 1) | SW ## n ; \
        if (debounce[s] == 0) { \
            /* down for at least 8 ticks */ \
            S ## n ## _PRESSED = 1; \
            if (!S ## n ## _LONG) { \
                switchcount[s]++; \
            } \
        } else { \
            /* released or bounced */ \
            if (S ## n ## _PRESSED) { \
                if (!S ## n ## _LONG) { \
                    ev = EV_S ## n ## _SHORT; \
                } \
                S ## n ## _PRESSED = 0; \
                S ## n ## _LONG = 0; \
                switchcount[s] = 0; \
            } \
        } \
        if (switchcount[s] > SW_CNTMAX) { \
            S ## n ## _LONG = 1; \
            switchcount[s] = 0; \
            ev = EV_S ## n ## _LONG; \
        } \
    }

    MONITOR_S(1);
    MONITOR_S(2);
    MONITOR_S(3);

    if (ev == EV_S1_LONG && S2_PRESSED) {
        S2_LONG = 1;
        switchcount[1] = 0;
        ev = EV_S1S2_LONG;
    } else if (ev == EV_S2_LONG && S1_PRESSED) {
        S1_LONG = 1;
        switchcount[0] = 0;
        ev = EV_S1S2_LONG;
    }
    if (event == EV_NONE) {
        event = ev;
    }
}

