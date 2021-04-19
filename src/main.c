//
// STC15F204EA DIY LED Clock
// Copyright 2016, Jens Jensen
//

// silence: "src/main.c:672: warning 126: unreachable code"
#pragma disable_warning 126

#include <stdint.h>
#include <stdio.h>
#include "stc15.h"
#include "uart.h"
#include "adc.h"
#include "ds1302.h"
#include "led.h"

//#define DEBUG

// clear wdt
#define WDT_CLEAR()    (WDT_CONTR |= 1 << 4)

// hardware configuration
#include "hwconfig.h"

uint8_t  temp;      // temperature sensor value
uint8_t  lightval;  // light sensor value

volatile uint8_t displaycounter;

volatile __bit S1_LONG;
volatile __bit S1_PRESSED;
volatile __bit S2_LONG;
volatile __bit S2_PRESSED;

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


/* This stores the last event.
 * After reading this should be set to EV_NONE,
 * new events will not be processed until it is set to EV_NONE */
static volatile enum Event event;

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

    /* The lower the compare the brighter the leds.
     * keep >=4 otherwise we loose a digit! */
    if(++displaycounter > 8)
        displaycounter = 0;

    // auto dimming, skip lighting for some cycles
    if (displaycounter < 4 ) {
        // display refresh ISR
        // cycle thru digits one at a time
        uint8_t digit = displaycounter;
        // fill digits
        LED_SEGMENT_PORT = dbuf[digit];
        // turn on selected digit, set low
        //LED_DIGIT_ON(digit);
        // issue #32, fix for newer sdcc versions which are using non-atomic port access
        tmp = ~((1<<LED_DIGITS_PORT_BASE) << digit);
        LED_DIGITS_PORT &= tmp;
    }

}

/* A overflowing couter couting in steps of 10ms */
static volatile uint8_t time_now;

/*
  interrupt: every 0.1ms=100us come here

  Check button status
  Dynamically LED turn on
 */
void timer0_isr() __interrupt 1 __using 1
{
    read_buttons();
    //display_scan_out();
    static uint8_t ms_10timer = 0;

    /* Count upto 10 ms */
    if(++ms_10timer > 100)
    {
        ms_10timer = 0;
        time_now++;
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

#if 0
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
#endif

enum StateMachine {
    SM_START,
    SM_BTN_INIT,
    SM_BTN_WAIT_FOR_RELEASE,
    SM_BTN_SETUP_GAME_TIME,
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

static uint8_t btn_is_pressed(void) {
    uint8_t adc = getADCResult8(ADC_LIGHT);
    return adc > 220;
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
    //BUZZER_ON;
}

static void beep_off(void)
{
    BUZZER_OFF;
}

static void send_assign(uint8_t your_id, uint8_t time)
{
    uart1_send_packet(OPC_ASSIGN, your_id, time);
}

static void send_passon(uint8_t ttl, uint8_t nr_of_players)
{
    uart1_send_packet(OPC_PASSON, ttl, nr_of_players);
}

static void send_claim(uint8_t id)
{
    uart1_send_packet(OPC_CLAIM, id, 'x');
}

static void print4char(const char* str)
{
    for(uint8_t i= 0; i < 4; i++)
    {
        uint8_t digit = *str++;
        if(digit < 'A') {
            digit -='0';
        } else {
            digit -= 'A' - LED_a;
        }
        filldisplay(i, digit);
    }
}

/* Works for :
 * seconds to minutes:seconds
 * minutes to hours:minutes
 *
 * It will not display a leading 0
 *
 * */
static void display_seconds_as_minutes(uint16_t time)
{
    uint8_t ten, dig, min = time / 60;
    ten = (min /  10 )%10;
    dig = (min /   1 )%10;
    if(ten)
        filldisplay(0, ten);
    filldisplay(1, dig);

    uint8_t sec = time % 60;
    ten = (sec /  10 )%10;
    dig = (sec /   1 )%10;
    filldisplay(2, ten);
    filldisplay(3, dig);

    /* Always turn on dots to show we have minutes and seconds */
    dotdisplay(1, 1);
    dotdisplay(2, 1);
}

/* Display an uint8_t on the last 3 digit */
static void display_val(uint8_t val)
{
    uint8_t hun = (val / 100 ) % 10;
    uint8_t ten = (val /  10 ) % 10;
    uint8_t dig = (val /   1 ) % 10;

    clearTmpDisplay();

    if(hun)
        filldisplay(1, hun);
    if(hun || ten)
        filldisplay(2, ten);

    filldisplay(3, dig);
}

/* The last state before we reached the panic state */
static enum StateMachine err;

static void panic_animation(void)
{
    display_val(err);
    filldisplay(0,'F' - 'A' + LED_a);
}

static void statemachine(void)
{
    static enum StateMachine state = SM_START;
    static uint8_t id = 0; //Default to 'master'
    static uint8_t nr_of_players = 0;
    static uint16_t seconds_left;
    static uint8_t game_duration_in_min = 30; //Default to 30 minutes
    static uint8_t beep_timer = 0;
    static uint8_t decrement_timer;
    static uint8_t msg_time = 0;

    clearTmpDisplay();

    if (timer_elapsed(&beep_timer)) {
        beep_off();
    } else {
        beep_on();
    }

    /* Save last state before panic so in panic we can show it */
    if(state != SM_PANIC)
    {
        err = state;
    }

    switch (state)
    {
        case SM_START:
            state = SM_BTN_INIT;
            break;

        case SM_BTN_INIT:
            if(0)
            {
                print4char("PRES");
            }
            else {
                /* Debug ADC */
                display_val(getADCResult8(ADC_LIGHT));
                filldisplay(0, 'P' - 'A' + LED_a);
            }
            if (btn_is_pressed()) {
                state = SM_BTN_WAIT_FOR_RELEASE;
                set_timer(&decrement_timer, 1 * TMO_SECOND);
            } else {
                state = SM_MSG_SLAVE;
            }
            break;

        case SM_BTN_WAIT_FOR_RELEASE:
            print4char("RELA");
            if(!btn_is_pressed() && timer_elapsed(&decrement_timer))
                state = SM_BTN_SETUP_GAME_TIME;
            break;

        case SM_BTN_SETUP_GAME_TIME:
            {
                /* Blink! */
                if(timer_elapsed(&decrement_timer))
                {
                    id = !id; //EVIL reuse of id
                    set_timer(&decrement_timer, 5 * TMO_100MS);
                }

                if(id) {
                    /* Also works for hours and minutes :D */
                    display_seconds_as_minutes(game_duration_in_min);
                } else {
                    clearTmpDisplay();
                }

                if(btn_is_pressed())
                {
                    /* We are master! kick off by sending assign */
                    id = 0;
                    seconds_left = game_duration_in_min * 60;
                    send_assign(id + 1, game_duration_in_min); //Next is player 1
                    set_timer(&beep_timer, 1 * TMO_10MS);
                    state = SM_MSG_MASTER;
                } else {
                    switch(event){
                        case EV_S1_SHORT:
                        case EV_S1_LONG:
                            if(game_duration_in_min < 90)
                                game_duration_in_min += 5;
                            break;
                        case EV_S2_SHORT:
                        case EV_S2_LONG:
                            if(game_duration_in_min > 5)
                                game_duration_in_min -= 5;
                            break;
                    }
                    event = EV_NONE;
                }
            }
            break;

        case SM_MSG_MASTER:
            print4char("ACCU");
            if (msg_available()) {
                state = SM_IS_ASSIGN_MASTER;
            }
            break;

        case SM_IS_ASSIGN_MASTER:
            /* Only accept assign if duration matches */
            if(rx_buf[0] == OPC_ASSIGN && rx_buf[2] == game_duration_in_min) {
                uint8_t ttl = 42; //Random...
                nr_of_players = rx_buf[1]; //last id
                send_passon(ttl, nr_of_players);
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
                /* Yeah! we got an assign message:
                 * save our id
                 * and the game time */
                id = rx_buf[1];
                game_duration_in_min = rx_buf[2];
                seconds_left = game_duration_in_min * 60;
                send_assign(id + 1, game_duration_in_min);
                state = SM_MSG;
            } else {
                state = SM_PANIC;
            }
            break;

        case SM_PANIC:
            set_timer(&beep_timer, 1 * TMO_10MS);
            panic_animation();
            break;

        case SM_MSG:
            /* Display duration of current active player (not us) */
            display_seconds_as_minutes(msg_time);
            if (msg_available()) {
                state = SM_IS_ASSIGN;
            }
            else {
                if(timer_elapsed(&decrement_timer)) {
                    msg_time++;
                    set_timer(&decrement_timer, 1 * TMO_SECOND);
                }
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
                send_claim(rx_buf[1]);
                state = SM_MSG;
            }
            //Counter reset voor display
            set_timer(&decrement_timer, 1 * TMO_SECOND);
            msg_time = 0;
            break;

        case SM_TTL_CHECK:
            {
                uint8_t ttl = rx_buf[1];
                nr_of_players = rx_buf[2];
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
            /* Show the number of players detected during countdown */
            filldisplay(0, 'P' - 'A' + LED_a);
            filldisplay(3, nr_of_players);

            if(timer_elapsed(&beep_timer)) {
                send_passon(rx_buf[1] - 1, nr_of_players);
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
                set_timer(&decrement_timer, 1 * TMO_SECOND);
                /* Always have atleast 60 seconds of play */
                if(seconds_left < 60)
                    seconds_left = 60;
                state = SM_BTN;
            } else {
                state = SM_PANIC;
            }
            break;

        case SM_BTN:
            /* Display duration of current active player (not us) */
            display_seconds_as_minutes(seconds_left);
            if (btn_is_pressed()) {
                set_timer(&beep_timer, 1 * TMO_10MS);
                send_passon(0, nr_of_players); // 0 = next
                state = SM_MSG;
            }
            else {
                if(timer_elapsed(&decrement_timer)) {
                    set_timer(&decrement_timer, 1 * TMO_SECOND);

                    if(seconds_left)
                        seconds_left--;
                    else
                        set_timer(&beep_timer, 1 * TMO_10MS);
                }
            }
            break;
    }

    /* If nothing on screen, show current state.
     * Usefull debugging aid. */
    if (tmpbuf[0]==LED_BLANK &&
        tmpbuf[1]==LED_BLANK &&
        tmpbuf[2]==LED_BLANK &&
        tmpbuf[3]==LED_BLANK)
    {
        display_val(state);
    }

    updateTmpDisplay();
}

/*********************************************/
int main()
{
    // SETUP
    // set photoresistor & ntc pins to open-drain output
    P1M1 |= (1<<ADC_LIGHT) | (1<<ADC_TEMP);
    P1M0 |= (1<<ADC_LIGHT) | (1<<ADC_TEMP);

    Timer0Init(); // display refresh & switch read

    uart1_init();   // setup uart

    EA = 1;         // Enable global interrupt

    // LOOP
    while (1)
    {
        display_scan_out();
        statemachine();

        __critical {
            updateTmpDisplay();
        }

        WDT_CLEAR();
    }
}
/* ------------------------------------------------------------------------- */
