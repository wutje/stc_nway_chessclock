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
extern volatile uint8_t EA;
extern volatile uint8_t WDT_CONTR;
#else
#include "stc15.h"
#endif

#include "timer0.h"
#include "uart.h"
#include "adc.h"
#include "led.h"
#include "buttons.h"

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
    SM_IS_MY_TURN,          //21
};

static uint8_t recovery_btn_is_pressed(void) {
    enum ButtonEvent ev = event;
    /* We handled it so clear! */
    event = EV_NONE;
    return ev == EV_S3_LONG;
}

static uint8_t btn_is_pressed(void) {
    enum ButtonEvent ev = event;
    /* We handled it so clear! */
    event = EV_NONE;
    return ev == EV_S3_SHORT;
}

static uint8_t msg_available(void) {
    uint8_t rx;
    /* Test&clear it atomically */
    __critical {
        rx = rx_packet_available;
        rx_packet_available = 0;
    }
    return rx;
}


enum RuntimeCfg
{
    RUN_CFG_BUZZER  = 1<<0,
    RUN_CFG_DEBUG   = 1<<1,
};
static enum RuntimeCfg cfg = RUN_CFG_BUZZER;

//only 4 clocks for now
#define MAX_NR_OF_PLAYERS 4
#define INIT_VALUE  (0xFF)

static uint8_t id; //my assigned ID
static uint8_t nr_of_players; //Detected number of players
static uint8_t active_player_id;
static uint16_t remaining_time[MAX_NR_OF_PLAYERS];

static void send_assign(uint8_t your_id, uint16_t cfg_time)
{
    uart1_send_packet(OPC_ASSIGN, your_id, nr_of_players, active_player_id, cfg_time);
}

static void send_passon(uint8_t ttl)
{
    uint8_t next_id = (id + 1) % nr_of_players;
    uint16_t rem_time = remaining_time[next_id];

    uart1_send_packet(OPC_PASSON, next_id, nr_of_players, ttl, rem_time);
}

static void send_claim(uint8_t id, uint16_t rem_time)
{
    uart1_send_packet(OPC_CLAIM, id, nr_of_players, cfg, rem_time);
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

static uint8_t beep_timer = 10 * TMO_100MS;

static void handle_beep(void)
{
    if (timer_elapsed(&beep_timer)) {
        BUZZER_OFF;
    } else {
        if(cfg & RUN_CFG_BUZZER) {
            BUZZER_ON;
        }
    }
}

static uint8_t save_claim_data(void)
{
    //CLAIM message
    uint8_t other_id = rx_buf[1];
    //nr_of_players    = rx_buf[2];
    cfg              = rx_buf[3];
    uint16_t secs = (uint16_t)rx_buf[4] << 8 | rx_buf[5];
    if(other_id != id) {
        /* keep track of its time, if it is valid.
         * Otherwise keep existing time.
         * This means we will send the 'original' time in a
         * claim message */
        if( secs < 90 * 60)
            remaining_time[other_id] = secs;
    }
    return other_id;
}

static void statemachine(void)
{
    static enum StateMachine state = SM_START;
    static uint8_t statemachine_delay = 0;
    static uint16_t seconds_left;
    static uint8_t game_duration_in_min;
    static uint8_t decrement_timer;
    static uint16_t other_player_time;
    static uint8_t cfg_state;

    /* If state machine should wait, do so */
    if(!timer_elapsed(&statemachine_delay)) {
        return;
    }

    /* Clear display AFTER check timer:
     * whoever sets the timer also has a one time option to set the screen. */
    clearTmpDisplay();

    /* Save last state before panic so in panic we can show it */
    if(state != SM_PANIC) {
        err = state;
    }

    switch (state)
    {
        case SM_START:
            /* Init 'global' variables */
            id = 0xFF;
            other_player_time = 0;
            game_duration_in_min = 30;
            active_player_id = INIT_VALUE;
            nr_of_players = 0;
            memset(remaining_time, 0xFF, sizeof(remaining_time));
            if(!btn_is_pressed())
                state = SM_BTN_INIT;
            break;

        case SM_BTN_INIT:
            switch(cfg_state) {
                case 0:
                    if(time_now & TICK_320MS) {
                        /* Also works for hours and minutes :D */
                        display_seconds_as_minutes(game_duration_in_min);
                    }

                    /* Go check for received data
                     * unless button 3 is pressed */
                    state = SM_MSG_SLAVE;

                    switch(event){
                        case EV_S1_SHORT:
                            if(game_duration_in_min < 90)
                                game_duration_in_min += 5;
                            break;
                        case EV_S2_SHORT:
                            if(game_duration_in_min > 5)
                                game_duration_in_min -= 5;
                            break;
                        case EV_S3_SHORT:
                            /* We are master! kick off by sending assign */
                            id = 0;
                            seconds_left = game_duration_in_min * 60;
                            for(uint8_t i = 0 ; i < MAX_NR_OF_PLAYERS; i++) {
                                remaining_time[i] = seconds_left;
                            }
                            send_assign(id + 1, seconds_left); //Next is player 1
                            set_timer(&beep_timer, 1 * TMO_10MS);
                            state = SM_MSG_MASTER;
                            break;
                        default:
                            break;
                    }
                    break;

                case 1:
                    display_val(!!(cfg & RUN_CFG_BUZZER));
                    display_char(0, 'B');

                    switch(event){
                        case EV_S1_SHORT:
                        case EV_S2_SHORT:
                            cfg ^= RUN_CFG_BUZZER;
                            break;
                        default:
                            break;
                    }
                break;

                case 2:
                    display_val(!!(cfg & RUN_CFG_DEBUG));
                    display_char(0, 'D');

                    switch(event){
                        case EV_S1_SHORT:
                        case EV_S2_SHORT:
                            cfg ^= RUN_CFG_DEBUG;
                            break;
                        default:
                            break;
                    }
            }

            if(event == EV_S1S2_LONG) {
                cfg_state++;
                if(cfg_state > 2)
                    cfg_state = 0;
            }

            /* We handled the event so clear it */
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
                send_passon(l);
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
                active_player_id = rx_buf[3];
                seconds_left = (((uint16_t)rx_buf[4]) << 8) | rx_buf[5];
                if(active_player_id == INIT_VALUE) {
                    for(uint8_t i = 0 ; i < MAX_NR_OF_PLAYERS; i++) {
                        remaining_time[i] = seconds_left;
                    }
                    /* Only send assign if no active player: aka during init */
                    send_assign(id + 1, seconds_left);
                }
                else {
                   /* nr of player is NOT valid on INIT_VALUE.
                    * Only after setup is it valid */
                   nr_of_players = rx_buf[2];
                }
                state = SM_IS_MY_TURN;
            } else {
                state = SM_IS_CLAIM_SLAVE;
            }
            break;

        case SM_IS_MY_TURN:
            if(active_player_id == id) {
                //Send claim since we are the current active player
                send_claim(id, seconds_left);
                state = SM_MSG_CLAIM;
            }
            else {
                state = SM_MSG;
            }
            break;

        case SM_IS_CLAIM_SLAVE:
            /* If this happens the game started but we did not see the ASSIGN.
             * Simply save other player data and keep waiting for PASSON. */
            if(rx_buf[0] == OPC_CLAIM) {
                uint8_t other_id = save_claim_data();
                send_claim(other_id, remaining_time[other_id]);
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

        /* Receving a passon while in SETUP state */
        case SM_PASS_CHECK_SLAVE:
            {
                if(rx_buf[3] == 0) { //ttl
                    /* Save data from this message!. It contains our id!
                     * PASSON */
                    id               = rx_buf[1]; //This my id, if ttl is 0
                    nr_of_players    = rx_buf[2];
                    //uint8_t ttl      = rx_buf[3];
                    seconds_left = (((uint16_t)rx_buf[4]) << 8) | rx_buf[5];
                    //Best guess, for next player
                    remaining_time[(id + 1) % nr_of_players] = seconds_left;
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
                /* Display remaining time of current active player (not us) */
                display_seconds_as_minutes(other_player_time);

                if(timer_elapsed(&decrement_timer)) {
                    other_player_time++;
                    remaining_time[active_player_id]--;
                    set_timer(&decrement_timer, 1 * TMO_SECOND);
                }
                state = SM_BTN_RECOVER;
            }
            break;

        case SM_IS_ASSIGN:
            if(rx_buf[0] == OPC_ASSIGN) {
                /* Drop it. Someone is pressing button but we are already okay. */
                state = SM_MSG;
            }else {
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
            ASSERT(rx_buf[0] == OPC_CLAIM);
            state = SM_CLAIM_CHECK;
            break;

        case SM_CLAIM_CHECK:
            {
                //CLAIM message
                uint8_t other_id = save_claim_data();
                if(other_id != id) {
                    /* Send message onto the assigned one.
                     * But keep track of its time */
                    active_player_id = other_id;
                    send_claim(other_id, remaining_time[other_id]);
                }
                //Counter reset voor display
                set_timer(&decrement_timer, 1 * TMO_SECOND);
                other_player_time = 0;
                state = SM_MSG;
            }
            break;

        case SM_TTL_CHECK:
            {
                //PASSON message
                //uint8_t r_id     = rx_buf[1]; //This my id, if ttl is 0
                nr_of_players    = rx_buf[2];
                uint8_t ttl      = rx_buf[3];
                //uint16_t secs    = rx_buf[4];
                //secs = secs << 8 | rx_buf[5];
                if(ttl == 0) {
                    send_claim(id, seconds_left);
                    set_timer(&beep_timer, 3 * TMO_100MS);
                    state = SM_MSG_CLAIM;
                } else {
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
                    send_passon(ttl - 1);
                    state = SM_MSG;
                }
            }
            break;

        case SM_MSG_CLAIM:
            print4char("RECO");
            if (msg_available()) {
                state = SM_IS_CLAIM2;
            } else {
                state = SM_BTN_RECOVER2;
            }
            break;

        case SM_BTN_RECOVER2:
            if(recovery_btn_is_pressed())
                send_claim(id, seconds_left);

            state = SM_MSG_CLAIM;
            break;

        case SM_IS_CLAIM2:
            ASSERT((rx_buf[0] == OPC_CLAIM) && (rx_buf[1] == id));

            /* We got OUR claim back. So lets start down counting! */
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
                send_passon(0); // ttl 0 = next
                set_timer(&beep_timer, 1 * TMO_10MS);
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

        case SM_BTN_RECOVER:
            /* Dirty hack: print the same as SM_MSG would do */
            //display_seconds_as_minutes(other_player_time);
            if(recovery_btn_is_pressed())
            {
                uint8_t next_id = (id + 1) % nr_of_players;
                send_assign(next_id, remaining_time[next_id]);
            }
            state = SM_MSG;
            break;
    }

    /* If nothing on screen, show current state.
     * Usefull debugging aid. */
    if ((cfg & RUN_CFG_DEBUG) &&
        tmpbuf[0]==LED_BLANK &&
        tmpbuf[1]==LED_BLANK &&
        tmpbuf[2]==LED_BLANK &&
        tmpbuf[3]==LED_BLANK)
    {
        display_val(state);
    }

    /* Copy buffer to buffer used by scan out */
    updateTmpDisplay();
}

int main()
{
    /* Init the hardware  */
    timer0_init();

    uart1_init();

    /* Enable interrupts, AFTER hardware setup */
    EA = 1;

    // LOOP
    while (1)
    {
        handle_beep();
        buttons_read();
        display_scan_out();
        statemachine();

        WDT_CLEAR();
    }
}
