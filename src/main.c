//
// STC15F204EA DIY LED Clock
// Copyright 2016, Jens Jensen
//

// silence: "src/main.c:672: warning 126: unreachable code"
#pragma disable_warning 126

#include <stdint.h>
#include <stdio.h>
#include "stc15.h"
#include "adc.h"
#include "ds1302.h"
#include "led.h"

//#define DEBUG

// clear wdt
#define WDT_CLEAR()    (WDT_CONTR |= 1 << 4)

// hardware configuration
#include "hwconfig.h"

// keyboard mode states
enum keyboard_mode {
    K_NORMAL,
};

// display mode states
enum display_mode {
    M_NORMAL,
    M_SET_HOUR_12_24,
#ifdef WITH_NMEA
    M_TZ_SET_TIME,
    M_TZ_SET_DST,
#endif
    M_SEC_DISP,
    M_TEMP_DISP,
#ifndef WITHOUT_DATE
    M_DATE_DISP,
#endif
    M_WEEKDAY_DISP,
    M_YEAR_DISP,
#ifndef WITHOUT_ALARM
    M_ALARM,
#endif
#ifndef WITHOUT_CHIME
    M_CHIME,
#endif
#ifdef DEBUG
    M_DEBUG,
    M_DEBUG2,
    M_DEBUG3,
#endif
};
#define NUM_DEBUG 3

#ifdef DEBUG
uint8_t hex[] = {0,1,2,3,4,5,6,7,8,9,14,15,16,17,18,19};
#endif

/* ------------------------------------------------------------------------- */
/*
void _delay_ms(uint8_t ms)
{
    // delay function, tuned for 11.092 MHz clock
    // optimized to assembler
    ms; // keep compiler from complaining?
    __asm;
        ; dpl contains ms param value
    delay$:
        mov	b, #8   ; i
    outer$:
        mov	a, #243    ; j
    inner$:
        djnz acc, inner$
        djnz b, outer$
        djnz dpl, delay$
    __endasm;
}
*/

uint8_t  temp;      // temperature sensor value
uint8_t  lightval;  // light sensor value

volatile uint8_t displaycounter;

volatile __bit S1_LONG;
volatile __bit S1_PRESSED;
volatile __bit S2_LONG;
volatile __bit S2_PRESSED;
volatile __bit S3_LONG;
volatile __bit S3_PRESSED;

volatile uint8_t debounce[NUM_SW];      // switch debounce buffer
volatile uint8_t switchcount[NUM_SW];
#define SW_CNTMAX 80	//long push

enum Event {
    EV_NONE,
    EV_S1_SHORT,
    EV_S1_LONG,
    EV_S2_SHORT,
    EV_S2_LONG,
    EV_S1S2_LONG,
    EV_S3_SHORT,
    EV_S3_LONG,
    EV_TIMEOUT,
};

#include "uart.h"

volatile enum Event event;

static void read_buttons(void)
{
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

static void display_scan_out(void)
{
    uint8_t tmp;

    // turn off all digits, set high
    LED_DIGITS_OFF();

    // auto dimming, skip lighting for some cycles
    if (displaycounter < 4 ) {
        // display refresh ISR
        // cycle thru digits one at a time
        uint8_t digit = displaycounter % (uint8_t) 4;
        // fill digits
        LED_SEGMENT_PORT = dbuf[digit];
        // turn on selected digit, set low
        //LED_DIGIT_ON(digit);
        // issue #32, fix for newer sdcc versions which are using non-atomic port access
        tmp = ~((1<<LED_DIGITS_PORT_BASE) << digit);
        LED_DIGITS_PORT &= tmp;
    }
    displaycounter++;
    if(displaycounter > 8)
        displaycounter = 0;
}

static volatile uint8_t time_now;
/*
  interrupt: every 0.1ms=100us come here

  Check button status
  Dynamically LED turn on
 */
void timer0_isr() __interrupt 1 __using 1
{
    //read_buttons();
    //display_scan_out();
    static uint8_t ms_10timer = 0;
    ms_10timer++;
    if(ms_10timer > 100)
    {
        time_now++;
        ms_10timer = 0;
    }
}

#define TMO_10MS 1
#define TMO_100MS 10
#define TMO_SECOND 100
static void set_timer(uint8_t *timer, uint8_t tmo)
{
    *timer = time_now + tmo;
}

/* Keep calling this function to make sure
 * the timer stays elapsed */
static uint8_t timer_elapsed(uint8_t *timer)
{
    uint8_t t = *timer;
    t -= time_now;
    if(t > 0x80)
    {
        *timer = time_now - 1;
        return 1;
    }
    return 0;
}

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

// Call timer0_isr() 10000/sec: 0.0001 sec
// Initialize the timer count so that it overflows after 0.0001 sec
// THTL = 0x10000 - FOSC / 12 / 10000 = 0x10000 - 92.16 = 65444 = 0xFFA4
// When 11.0592MHz clock case, set every 100us interruption
static void Timer0Init(void)		//100us @ 11.0592MHz
{
    // refer to section 7 of datasheet: STC15F2K60S2-en2.pdf
    // TMOD = 0;    // default: 16-bit auto-reload
    // AUXR = 0;    // default: traditional 8051 timer frequency of FOSC / 12
    // Initial values of TL0 and TH0 are stored in hidden reload registers: RL_TL0 and RL_TH0
    TL0 = 0xA4;		// Initial timer value
    TH0 = 0xFF;		// Initial timer value
    TF0 = 0;		// Clear overflow flag
    TR0 = 1;		// Timer0 start run
    ET0 = 1;        // Enable timer0 interrupt
}

// Formula was : 76-raw*64/637 - which makes use of integer mult/div routines
// Getting degF from degC using integer was not good as values were sometimes jumping by 2
// The floating point one is even worse in term of code size generated (>1024bytes...)
// Approximation for slope is 1/10 (64/637) - valid for a normal 20 degrees range
// & let's find some other trick (80 bytes - See also docs\Temp.ods file)
int8_t gettemp(uint16_t raw) {
    uint16_t val=raw;
    uint8_t temp;

    raw<<=2;
    if (CONF_C_F) raw<<=1;  // raw*5 (4+1) if Celcius, raw*9 (4*2+1) if Farenheit
    raw+=val;

    if (CONF_C_F) {val=6835; temp=32;}  // equiv. to temp=xxxx-(9/5)*raw/10 i.e. 9*raw/50
                                        // see next - same for degF
             else {val=5*757; temp=0;}  // equiv. to temp=xxxx-raw/10 or which is same 5*raw/50
                                        // at 25degC, raw is 512, thus 24 is 522 and limit between 24 and 25 is 517
                                        // so between 0deg and 1deg, limit is 517+24*10 = 757
                                        // (*5 due to previous adjustment of raw value)
    while (raw<val) {temp++; val-=50;}

    return temp + (cfg_table[CFG_TEMP_BYTE] & CFG_TEMP_MASK) - 4;
}


enum StateMachine {
    SM_START,
    SM_BTN_INIT,
    SM_MSG_MASTER,
    SM_IS_ASSIGN_MASTER,
    SM_MSG_SLAVE,
    SM_IS_ASSIGN_SLAVE,
    SM_PANIC,
    SM_MSG,
    SM_IS_ASSIGN,
    SM_IS_PASS,
    SM_IS_CLAIM,
    SM_CLAIM_CHECK,
    SM_TTL_CHECK,
    SM_MSG_CLAIM,
    SM_IS_CLAIM2,
    SM_BTN,
    SM_TTL_CHECK_TIMEOUT,
};

static void btn_clear(void) {
    event = EV_NONE;
}

static uint8_t btn_is_pressed(void) {
    enum Event ev;

    ev = event;
    event = EV_NONE;
    switch(ev)
    {
        case EV_S3_SHORT:
        case EV_S3_LONG:
            return 1;
    }
    return 0;
}

static uint8_t msg_available(void) {
    uint8_t rx;
    __critical {
        rx = rx_packet_available;
        rx_packet_available = 0;
    }
    return rx;
}

static void beep_on(void)
{
    BUZZER_ON;
}

static void beep_off(void)
{
    BUZZER_OFF;
}

static void send_assign(uint8_t your_id, uint8_t time)
{
    uart1_send_packet(OPC_ASSIGN, your_id, time);
}

static void send_passon(uint8_t ttl)
{
    uart1_send_packet(OPC_PASSON, ttl, 'x');
}

static void send_claim(uint8_t id)
{
    uart1_send_packet(OPC_CLAIM, id, 'x');
}

static void statemachine(void)
{
    static enum StateMachine state = SM_START;
    static uint8_t id;
    static uint8_t remaining_time;
    static uint8_t beep_timer = 0;
    static uint8_t run_timer = 0;

    if (timer_elapsed(&beep_timer)) {
        beep_off();
    } else {
        beep_on();
    }

    if (!timer_elapsed(&run_timer))
        return;

    //set_timer(&run_timer, 20 * TMO_10MS);

    clearTmpDisplay();
    filldisplay(2, state / 10, 0);
    filldisplay(3, state % 10, 0);
    updateTmpDisplay();

    switch (state)
    {
        case SM_START:
            state = SM_BTN_INIT;
            btn_clear();
            break;

        case SM_BTN_INIT:
            if (btn_is_pressed()) {
                id = 'W';
                send_assign(id + 1, 30); //Player 1, 30 minutes
                state = SM_MSG_MASTER;
            } else {
                state = SM_MSG_SLAVE;
            }
            break;

        case SM_MSG_MASTER:
            if (msg_available()) {
                state = SM_IS_ASSIGN_MASTER;
            }
            break;

        case SM_IS_ASSIGN_MASTER:
            if(rx_buf[0] == OPC_ASSIGN) {
                uint8_t ttl = 42 / 8 / 2; //Random...
                ttl = 200;
                send_passon(ttl);
                state = SM_MSG;
            } else {
                state = SM_PANIC;
            }
            break;

        case SM_MSG_SLAVE:
            if (msg_available()) {
                state = SM_IS_ASSIGN_SLAVE;
            } else {
                state = SM_BTN_INIT;
            }
            break;

        case SM_IS_ASSIGN_SLAVE:
            if(rx_buf[0] == OPC_ASSIGN) {
                id = rx_buf[1];
                remaining_time = rx_buf[2];
                send_assign(id + 1, remaining_time);
                state = SM_MSG;
            } else {
                state = SM_PANIC;
            }
            break;

        case SM_PANIC:
            //panic_animation();
            break;

        case SM_MSG:
            if (msg_available()) {
                state = SM_IS_ASSIGN;
            } else {
                state = SM_MSG;
                //DO STUFF WITH DISPLAY
            }
            break;

        case SM_IS_ASSIGN:
            if(rx_buf[0] == OPC_ASSIGN) {
                state = SM_PANIC;
            } else {
                state = SM_IS_PASS;
            }
            break;

        case SM_IS_PASS:
            if(rx_buf[0] == OPC_PASSON) {
                state = SM_TTL_CHECK;
            } else {
                state = SM_IS_CLAIM;
            }
            break;

        case SM_IS_CLAIM:
            if(rx_buf[0] == OPC_CLAIM) {
                state = SM_CLAIM_CHECK;
            } else {
                state = SM_PANIC;
            }
            break;

        case SM_CLAIM_CHECK:
            if(rx_buf[1] == id) {
                state = SM_MSG;
            } else {
                //Counter reset voor display
                send_claim(rx_buf[1]);
                state = SM_MSG;
            }
            break;

        case SM_TTL_CHECK:
            {
                uint8_t ttl = rx_buf[1];
                if(ttl == 0) {
                    send_claim(id);
                    set_timer(&beep_timer, 3 * TMO_100MS);
                    state = SM_MSG_CLAIM;
                } else {
                    set_timer(&beep_timer, ((255 - ttl) / 10) * TMO_10MS);
                    state = SM_TTL_CHECK_TIMEOUT;
                }
            }
            break;

        case SM_TTL_CHECK_TIMEOUT:
            if(timer_elapsed(&beep_timer)) {
                send_passon(rx_buf[1] - 1);
                state = SM_MSG;
            }
            break;

        case SM_MSG_CLAIM:
            if (msg_available()) {
                state = SM_IS_CLAIM2;
            }
            break;

        case SM_IS_CLAIM2:
            if(rx_buf[0] == OPC_CLAIM) {
                state = SM_BTN;
                btn_clear();
            } else {
                state = SM_PANIC;
            }
            break;

        case SM_BTN:
            if (btn_is_pressed()) {
                state = SM_MSG;
                send_passon(0); // 0 = next
            }
            else {
                //TODO decrement timer
            }
            break;
    }
}

/*********************************************/
int main()
{
    // SETUP
    // set photoresistor & ntc pins to open-drain output
    P1M1 |= (0<<ADC_LIGHT) | (1<<ADC_TEMP) | (1<<5);
    P1M0 |= (0<<ADC_LIGHT) | (1<<ADC_TEMP) | (1<<5);

    Timer0Init(); // display refresh & switch read

    uart1_init();   // setup uart

    EA = 1;         // Enable global interrupt

    // LOOP
    while (1)
    {
        static uint16_t tick_counter = 0;
        tick_counter++;
        display_scan_out();
        read_buttons();
        statemachine();

        __critical {
            updateTmpDisplay();
        }

        WDT_CLEAR();
    }
}
/* ------------------------------------------------------------------------- */
