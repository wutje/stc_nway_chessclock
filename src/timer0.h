#ifndef TIMER0_H
#define TIMER0_H

#include <stdbool.h>
#include <stdint.h>

/* A overflowing counter couting in steps of 10ms
 * Bit 0 = 10ms
 * Bit 1 = 20ms
 * Bit 2 = 40ms
 * Bit 3 = 80ms
 * Bit 4 = 160ms
 * Bit 5 = 320ms
 * Bit 6 = 640ms
 * Bit 7 = 1280ms */
#define TICK_10MS  (1<<0)
#define TICK_320MS  (1<<5)
#define TICK_640MS  (1<<6)
#define TICK_1280MS  (1<<7)
extern volatile uint8_t time_now;
void timer0_init(void);
#ifndef __GNUC__
void timer0_isr() __interrupt 1 __using 1;
#endif

bool timer_elapsed(uint8_t *timer);

#define TMO_10MS 1
#define TMO_100MS 10
#define TMO_SECOND 100
static inline void set_timer(uint8_t *timer, uint8_t tmo)
{
    *timer = time_now + tmo;
}

#endif /* TIMER0_H */
