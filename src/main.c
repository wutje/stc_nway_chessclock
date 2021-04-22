//
// STC15F204EA DIY LED Clock
// Copyright 2016, Jens Jensen
//

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef __GNUC__
#define __bit uint8_t
#define __interrupt
#define __at(_1)
#define __critical
extern volatile uint8_t P2 ;
extern volatile uint8_t P3 ;
extern volatile uint8_t P1_6 ;
extern volatile uint8_t P3_1 ;
extern volatile uint8_t P3_0 ;
extern volatile uint8_t P1M0 ;
extern volatile uint8_t P1M1 ;
extern volatile uint8_t EA;
extern volatile uint8_t WDT_CONTR;
#else
#include "stc15.h"
#endif

#include "uart.h"
#include "adc.h"
#include "timer0.h"
#include "led.h"

//#define DEBUG

// clear wdt
#define WDT_CLEAR()    (WDT_CONTR |= 1 << 4)

//Jump to panic state
#define ASSERT(x) {if (!(x)) {state = SM_PANIC; break;} }

// hardware configuration
#include "hwconfig.h"

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


volatile __bit S1_LONG;
volatile __bit S1_PRESSED;
volatile __bit S2_LONG;
volatile __bit S2_PRESSED;
volatile __bit S3_LONG;
volatile __bit S3_PRESSED;

volatile uint8_t debounce[NUM_SW];      // switch debounce buffer
volatile uint8_t switchcount[NUM_SW];
#define SW_CNTMAX (80*1)	//long push

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
    static uint8_t t = 0;
    /* Run only every 10ms */
    if(t != (time_now&1))
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

static void display_scan_out(void)
{
    uint8_t tmp;
    static uint8_t displaycounter = 0;

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

#if 0
uint8_t  temp;      // temperature sensor value
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
    SM_BTN,                 //15
    SM_IS_CLAIM_SLAVE,      //16
    SM_IS_PASS_SLAVE,       //17
    SM_PASS_CHECK_SLAVE,    //18
    SM_BTN_RECOVER,         //19
    SM_BTN_RECOVER2,        //20
};

static uint8_t recovery_btn_is_pressed(void) {
    enum Event ev = event;
    /* We handled it so clear! */
    event = EV_NONE;
    return ev == EV_S3_LONG;
}

static uint8_t btn_is_pressed(void) {
    enum Event ev = event;
    /* We handled it so clear! */
    event = EV_NONE;
    return ev == EV_S3_SHORT;
}

static uint8_t msg_available(void) {
    uint8_t rx;
    /* Test&clear in atomically */
    __critical {
        rx = rx_packet_available;
        rx_packet_available = 0;
    }
    return rx;
}

static void send_assign(uint8_t your_id, uint16_t cfg_time)
{
    uart1_send_packet(OPC_ASSIGN, your_id, 'x', 'x', cfg_time);
}

static void send_passon(uint8_t ttl, uint8_t next_id, uint8_t nr_of_players, uint16_t rem_time)
{
    uart1_send_packet(OPC_PASSON, next_id, nr_of_players, ttl, rem_time);
}

static void send_claim(uint8_t id, uint8_t nr_of_players, uint16_t rem_time)
{
    uart1_send_packet(OPC_CLAIM, id, nr_of_players, 'x', rem_time);
}

/* Displaying chars is non-trivial, so I added this convenience macro */
#define display_char(_pos, _char) {filldisplay(_pos, _char - 'A' + LED_a); }

static void print4char(const char* str)
{
    for(uint8_t i = 0; i < 4; i++)
    {
        uint8_t digit = *str++;
        if(digit < 'A') {
            filldisplay(i, digit - '0');
        } else {
            display_char(i, digit);
        }
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

    /* Do not display any leading zero */
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
    display_char(0, 'F');
}

static uint8_t beep_timer = 0;

static void handle_beep(void)
{
    if (timer_elapsed(&beep_timer)) {
        BUZZER_OFF;
    } else {
        if(0) {
            BUZZER_ON;
        }
    }
}

static void statemachine(void)
{
    static enum StateMachine state = SM_START;
    static uint8_t id;
    static uint8_t nr_of_players;
    static uint16_t seconds_left;
    static uint8_t game_duration_in_min;
    static uint8_t decrement_timer;
    static uint8_t other_player_time;
    static uint8_t active_player_id;
    static uint8_t statemachine_delay = 0;
    static uint8_t remaining_time[4]; //4 clocks for now

    /* If state machine should wait, do so */
    if(!timer_elapsed(&statemachine_delay)) {
        return;
    }

    clearTmpDisplay();

    /* Save last state before panic so in panic we can show it */
    if(state != SM_PANIC) {
        err = state;
    }

    switch (state)
    {
        case SM_START:
            id = 0;
            other_player_time = 0;
            game_duration_in_min = 30;
            nr_of_players = 0;
            memset(remaining_time, 0, sizeof(remaining_time));
            if(!btn_is_pressed())
                state = SM_BTN_INIT;
            break;

        case SM_BTN_INIT:
            if(time_now & TICK_320MS) {
                /* Also works for hours and minutes :D */
                display_seconds_as_minutes(game_duration_in_min);
            } else {
                clearTmpDisplay();
            }

            /* Go check for received data
             * unless button 3 is pressed */
            state = SM_MSG_SLAVE;

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
                case EV_S3_SHORT:
                case EV_S3_LONG:
                    /* We are master! kick off by sending assign */
                    id = 0;
                    seconds_left = game_duration_in_min * 60;
                    send_assign(id + 1, seconds_left); //Next is player 1
                    set_timer(&beep_timer, 1 * TMO_10MS);
                    state = SM_MSG_MASTER;
                    break;
                default:
                    break;
            }
            /* We handled the event so clear it */
            if(event != EV_NONE)
                event = EV_NONE;

            break;

        case SM_MSG_MASTER:
            print4char("DEAD");
            if (msg_available()) {
                state = SM_IS_ASSIGN_MASTER;
            }
            break;

        case SM_IS_ASSIGN_MASTER:
            /* Only accept assign if duration matches */
            ASSERT(rx_buf[0] == OPC_ASSIGN );
            {
                uint8_t l = 42; //Random...
                nr_of_players = rx_buf[1]; //last id
                uint8_t next_id = (id + 1) % nr_of_players;
                send_passon(l, next_id, nr_of_players, seconds_left);
                state = SM_MSG;
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
                seconds_left = (uint16_t)rx_buf[3] << 8 | rx_buf[4];
                send_assign(id + 1, seconds_left);
                state = SM_MSG;
            } else {
                state = SM_IS_CLAIM_SLAVE;
            }
            break;

        case SM_IS_CLAIM_SLAVE:
            if(rx_buf[0] == OPC_CLAIM) {
                /* Forward message as is */
                //CLAIM message
                uint8_t other_id = rx_buf[1];
                nr_of_players    = rx_buf[2];
                //                 rx_buf[3]; //Unused for now
                uint16_t secs    = rx_buf[4];
                secs = secs << 8 | rx_buf[5];
                send_claim(other_id, nr_of_players, secs);
                state = SM_MSG_SLAVE;
            } else {
                state = SM_IS_PASS_SLAVE;
            }
            break;

        case SM_IS_PASS_SLAVE:
            /* If anything other then PASSON give up, we tried all. */
            ASSERT(rx_buf[0] == OPC_PASSON );
            state = SM_PASS_CHECK_SLAVE;
            break;

        case SM_PASS_CHECK_SLAVE:
            {
                if(rx_buf[3] == 0) { //ttl
                    /* Save data from this message!
                     * PASSON */
                    id               = rx_buf[1]; //This my id, if ttl is 0
                    nr_of_players    = rx_buf[2];
                    uint8_t ttl      = rx_buf[3];
                    uint16_t secs    = rx_buf[4];
                    secs = secs << 8 | rx_buf[5]; //This is MY remaining time
                    seconds_left = secs;
                    state = SM_MSG_CLAIM;
                }
                else
                    state = SM_MSG_SLAVE;
            }
            break;

        case SM_PANIC:
            set_timer(&beep_timer, 1 * TMO_10MS);
            panic_animation();
            break;

        case SM_MSG:
            if (msg_available()) {
                state = SM_IS_ASSIGN;
            } else {
                if(0 && (time_now & TICK_1280MS)) {
                    /* Display remaining time of current active player (not us) */
                    display_seconds_as_minutes(remaining_time[active_player_id]);
                } else {
                    /* Display remaining time of current active player (not us) */
                    display_seconds_as_minutes(other_player_time);
                }

                if(timer_elapsed(&decrement_timer)) {
                    other_player_time++;
                    remaining_time[active_player_id]--;
                    set_timer(&decrement_timer, 1 * TMO_SECOND);
                }
            }
            break;

        case SM_IS_ASSIGN:
            ASSERT(rx_buf[0] != OPC_ASSIGN);
            state = SM_IS_PASS;
            break;

        case SM_IS_PASS:
            if(rx_buf[0] == OPC_PASSON) {
                state = SM_TTL_CHECK;
            } else {
                state = SM_IS_CLAIM;
            }
            break;

        case SM_IS_CLAIM:
            ASSERT(rx_buf[0] == OPC_CLAIM);
            state = SM_CLAIM_CHECK;
            break;

        case SM_CLAIM_CHECK:
            {
                //CLAIM message
                uint8_t other_id = rx_buf[1];
                nr_of_players    = rx_buf[2];
                //                 rx_buf[3]; //Unused for now
                uint16_t secs    = rx_buf[4];
                secs = secs << 8 | rx_buf[5];
                if(other_id == id) {
                    state = SM_MSG;
                } else {
                    /* Send message onto the assigned one. But keep track
                     * of its time */
                    remaining_time[other_id] = secs;
                    active_player_id = other_id;
                    send_claim(other_id, nr_of_players, secs);
                    state = SM_MSG;
                }
                //Counter reset voor display
                set_timer(&decrement_timer, 1 * TMO_SECOND);
                other_player_time = 0;
            }
            break;

        case SM_TTL_CHECK:
            {
                //PASSON message
                uint8_t r_id     = rx_buf[1]; //This my id, if ttl is 0
                nr_of_players    = rx_buf[2];
                uint8_t ttl      = rx_buf[3];
                uint16_t secs    = rx_buf[4];
                secs = secs << 8 | rx_buf[5];
                if(ttl == 0) {
                    send_claim(r_id, nr_of_players, secs);
                    set_timer(&beep_timer, 3 * TMO_100MS);
                    state = SM_MSG_CLAIM;
                } else {
                    uint8_t next_id = (id + 1) % nr_of_players;
                    /* TTL != 0 means we are in discovery mode! */
                    uint8_t tmo = ((255 - ttl) / 10) * TMO_10MS;
                    set_timer(&beep_timer, tmo);

                    /* Add 'silence' by waiting a little longer before continuing */
                    set_timer(&statemachine_delay, tmo + 2 * TMO_10MS);

                    /* Show the number of players detected during countdown:
                     * for player 1 of 2 it show "P0-2" */
                    display_char(0, 'P');
                    filldisplay(1, id);
                    filldisplay(2, LED_DASH);
                    filldisplay(3, nr_of_players);

                    /* Send message straight away,
                     * we will wait before processing another */
                    send_passon(ttl - 1, next_id, nr_of_players, seconds_left);
                    state = SM_MSG;
                }
            }
            break;

        case SM_MSG_CLAIM:
            if (msg_available()) {
                state = SM_IS_CLAIM2;
            } else {
                state = SM_BTN_RECOVER2;
            }
            break;

        case SM_BTN_RECOVER2:
            if(recovery_btn_is_pressed())
                send_claim(id, nr_of_players, seconds_left);

            state = SM_MSG_CLAIM;
            break;

        case SM_IS_CLAIM2:
            ASSERT((rx_buf[0] == OPC_CLAIM));
            /* TODO save data in claim in case we are not yet booted */
            set_timer(&decrement_timer, 1 * TMO_SECOND);
            /* Always have atleast 60 seconds of play */
            if(seconds_left < 60)
                seconds_left = 60;
            state = SM_BTN;
            break;

        case SM_BTN:
            /* Display duration of current active player (not us) */
            display_seconds_as_minutes(seconds_left);
            if (btn_is_pressed()) {
                uint8_t next_id = (id + 1) % nr_of_players;
                send_passon(0, next_id, nr_of_players, remaining_time[next_id]); // 0 = next
                set_timer(&beep_timer, 1 * TMO_10MS);
                state = SM_BTN_RECOVER;
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

        case SM_BTN_RECOVER:
            if(recovery_btn_is_pressed())
            {
                uint8_t next_id = (id + 1) % nr_of_players;
                send_passon(0, next_id, nr_of_players, remaining_time[next_id]); // 0 = next
            }
            state = SM_MSG;
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

    /* Copy buffer to buffer used by ISR to scan out */
    __critical {
        updateTmpDisplay();
    }

}

/*********************************************/
int main()
{
    // SETUP
    // set photoresistor & ntc pins to open-drain output
    P1M1 |= (0<<ADC_LIGHT) | (1<<ADC_TEMP);
    P1M0 |= (0<<ADC_LIGHT) | (1<<ADC_TEMP);

    timer0_init(); // display refresh & switch read

    uart1_init();   // setup uart

    EA = 1;         // Enable global interrupt

    // LOOP
    while (1)
    {
        handle_beep();
        read_buttons();
        display_scan_out();
        statemachine();

        WDT_CLEAR();
    }
}
/* ------------------------------------------------------------------------- */
