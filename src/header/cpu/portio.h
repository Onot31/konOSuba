#ifndef _PORTIO_H
#define _PORTIO_H

#include <stdint.h>

/**
 * out - Send data to I/O port (8-bit)
 * @param port  I/O port number
 * @param data  Data to send
 */
void out(uint16_t port, uint8_t data);

/**
 * in - Receive data from I/O port (8-bit)
 * @param port  I/O port number
 * @return Data received
 */
uint8_t in(uint16_t port);

/**
 * out16 - Send data to I/O port (16-bit)
 * @param port  I/O port number
 * @param data  Data to send
 */
void out16(uint16_t port, uint16_t data);

/**
 * in16 - Receive data from I/O port (16-bit)
 * @param port  I/O port number
 * @return Data received
 */
uint16_t in16(uint16_t port);

#endif