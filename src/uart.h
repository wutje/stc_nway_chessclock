#ifndef UART_H
#define UART_H

enum OPC {
    OPC_ASSIGN = 'A',
    OPC_PASSON = 'P',
    OPC_CLAIM  = 'C',
    OPC_PANIC,
};

/* Packet size is OPC + data bytes */
#define MAX_PACKET_SIZE 6

//If this bit is set a new packet is available in RX_BUF
extern volatile __bit rx_packet_available;
extern uint8_t rx_buf[MAX_PACKET_SIZE];

void uart1_init(void);
void uart1_send_packet(uint8_t opc, uint8_t data0, uint8_t data1, uint8_t data2, uint16_t data34);
void uart1_send_byte(uint8_t b);

//Because it is needed in the file containing main
#ifndef __GNUC__
void uart1_isr() __interrupt 4 __using 2;
#endif
#endif
