#ifndef TIMER1_H
#define TIMER1_H

/* A overflowing counter couting in steps of 10ms
 * Bit 0 = 10ms
 * Bit 1 = 20ms
 * Bit 2 = 40ms
 * Bit 3 = 80ms
 * Bit 4 = 160ms
 * Bit 5 = 320ms
 * Bit 6 = 640ms
 * Bit 7 = 1280ms */
#define TICK_320MS  (1<<5)
#define TICK_640MS  (1<<6)
#define TICK_1280MS  (1<<7)
extern volatile uint8_t time_now;
void timer0_init(void);
#ifndef __GNUC__
void timer0_isr() __interrupt 1 __using 1;
#endif

#endif /* TIMER1_H */
