#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "stc15.h"

#include "uart.h"

/* Protocol on the wire:
 * 1 byte SYNC
 * 1 byte CNTR //Debug aid
 * 1 byte OPC
 * 1 byte DATA0
 * 1 byte DATA1
 * 1 byte DATA2
 * 1 byte DATA3
 * 1 byte DATA4
 * 1 byte CHECKSUM
 *
 * This is a total of 9 bytes.
*/

#define SYNC_BYTE 's'
enum ISR_STATE {
    ISR_STATE_SYNC,
    ISR_STATE_CNTR,
    ISR_STATE_OPC,
    ISR_STATE_DATA0,
    ISR_STATE_DATA1,
    ISR_STATE_DATA2,
    ISR_STATE_DATA3,
    ISR_STATE_DATA4,
    ISR_STATE_CHECKSUM,
};

#define BAUDRATE 9600 // serial port speed

uint8_t rx_buf[MAX_PACKET_SIZE];
volatile __bit rx_packet_available = 0;

static volatile uint8_t tx_busy = 0;
static uint8_t tx_buf[MAX_PACKET_SIZE + 1]; //+1 for checksum
static enum ISR_STATE isr_tx_state;

void uart1_init(void)
{
    //P_SW1 and P_SW0 define the pins used by the UART.
    //Other location might be routed to pins, depending on the PCB
    //  00    P3.0 and P3.1 the ones used for flashing.
    //  01    ?
    //  10    Using P3_6.
    //P_SW1 |= (1 << 6);          // move UART1 pins -> P3_6:rxd, P3_7:txd
    // UART1 use Timer2
    T2L = (65536 - (FOSC / 4 / BAUDRATE)) & 0xFF;
    T2H = (65536 - (FOSC / 4 / BAUDRATE)) >> 8;
    SM1 = 1;                    // serial mode 1: 8-bit async
    AUXR |= 0x14;               // T2R: run T2, T2x12: T2 clk src sysclk/1
    AUXR |= 0x01;               // S1ST2: T2 is baudrate generator
    ES = 1;                     // enable uart1 interrupt
    REN = 1;
}

static uint8_t calc_checksum(const uint8_t *data, uint8_t len)
{
    uint8_t checksum = SYNC_BYTE;
    for(uint8_t i = 0; i < len; i++)
        checksum += data[i];
    return checksum;
}

void uart1_isr() __interrupt 4 __using 2
{
    static uint8_t cntr = 0;
    /* Receive interrupt */
    if (RI) {
        static enum ISR_STATE isr_rx_state = ISR_STATE_SYNC;
        RI = 0;                 // clear inta
        /* Read byte from UART */
        uint8_t rx_byte = SBUF;
        switch(isr_rx_state)
        {
            case ISR_STATE_SYNC:
                if (rx_byte == SYNC_BYTE)
                    isr_rx_state++;
                break;

            case ISR_STATE_CNTR:  cntr = rx_byte;      isr_rx_state++; break;
            case ISR_STATE_OPC:   rx_buf[0] = rx_byte; isr_rx_state++; break;
            case ISR_STATE_DATA0: rx_buf[1] = rx_byte; isr_rx_state++; break;
            case ISR_STATE_DATA1: rx_buf[2] = rx_byte; isr_rx_state++; break;
            case ISR_STATE_DATA2: rx_buf[3] = rx_byte; isr_rx_state++; break;
            case ISR_STATE_DATA3: rx_buf[4] = rx_byte; isr_rx_state++; break;
            case ISR_STATE_DATA4: rx_buf[5] = rx_byte; isr_rx_state++; break;

            case ISR_STATE_CHECKSUM:
                if(calc_checksum(rx_buf, MAX_PACKET_SIZE) != rx_byte) {
                    //PANIC MODE?
                    rx_buf[0] = OPC_PANIC;
                }
                rx_packet_available = 1;
                //Restart statemachine
                isr_rx_state = ISR_STATE_SYNC;
                break;
        }
    }

    /* Transmit interrupt */
    if (TI) {
        TI = 0;
        tx_busy = 0;
        switch(isr_tx_state)
        {
            case ISR_STATE_SYNC:
                //IDLE!
                break;

            case ISR_STATE_CNTR:  SBUF = cntr + 1;  isr_tx_state++; break;
            case ISR_STATE_OPC:   SBUF = tx_buf[0]; isr_tx_state++; break;
            case ISR_STATE_DATA0: SBUF = tx_buf[1]; isr_tx_state++; break;
            case ISR_STATE_DATA1: SBUF = tx_buf[2]; isr_tx_state++; break;
            case ISR_STATE_DATA2: SBUF = tx_buf[3]; isr_tx_state++; break;
            case ISR_STATE_DATA3: SBUF = tx_buf[4]; isr_tx_state++; break;
            case ISR_STATE_DATA4: SBUF = tx_buf[5]; isr_tx_state++; break;

            case ISR_STATE_CHECKSUM:
                SBUF = tx_buf[MAX_PACKET_SIZE];
                isr_tx_state = ISR_STATE_SYNC;
                break;
        }
    }
}

void uart1_send_byte(uint8_t b)
{
    while(tx_busy);
    tx_busy = 1;
    SBUF = b;
}

void uart1_send_packet(uint8_t opc, uint8_t data0, uint8_t data1, uint8_t data2, uint16_t data34)
{
    /* Link TX to RX internally */
    if(0) {
        rx_buf[0] = opc;
        rx_buf[1] = data0;
        rx_buf[2] = data1;
        rx_buf[3] = data2;
        rx_buf[4] = data34 >> 8;
        rx_buf[5] = data34 & 0xFF;
        rx_packet_available = 1;
    }
    /* Normal code */
    else
    {
        rx_packet_available = 0; //Clear just in case
        tx_buf[0] = opc;
        tx_buf[1] = data0;
        tx_buf[2] = data1;
        tx_buf[3] = data2;
        tx_buf[4] = data34 >> 8;
        tx_buf[5] = data34 & 0xFF;
        tx_buf[MAX_PACKET_SIZE] = calc_checksum(tx_buf, MAX_PACKET_SIZE);
        /* Start ISR by sending the first byte */
        isr_tx_state = ISR_STATE_CNTR;
        SBUF = SYNC_BYTE;
    }
}

