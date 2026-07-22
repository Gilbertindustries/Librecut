/*
 * usb.h
 *
 * Header interface for USB UART driver communicating with FTDI interface chip.
 *
 * Copyright 2010 <freecutfirmware@gmail.com> 
 *
 * This file is part of Freecut.
 *
 * Freecut is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2.
 *
 * Freecut is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Freecut. If not, see http://www.gnu.org/licenses/.
 */

#ifndef USB_H
#define USB_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Global C standard library I/O stream handle for fprintf/fscanf operations
extern FILE usb;

/**
 * @brief Initializes USART1 hardware, enables RX/TX interrupt vectors, and sets 115200 8N1 default mode.
 */
void usb_init( void );

/**
 * @brief Dynamically recalculates UBRR1 registers to adjust USART1 baud rate.
 * @param baud Target baud rate (e.g., 9600, 57600, 115200).
 */
void usb_set_baud( long baud );

/**
 * @brief Adds a character to the transmit ring buffer and enables data register empty interrupt.
 * Converts '\n' to '\r\n' automatically.
 * @param c Character byte to send.
 * @param stream Pointer to FILE stream context.
 * @return 0 on successful enqueuing.
 */
int usb_putchar( char c, FILE *stream );

/**
 * @brief Blocking call that waits for a character to become available in the receive ring buffer.
 * @param stream Pointer to FILE stream context.
 * @return Received character byte as integer.
 */
int usb_getchar( FILE *stream );

/**
 * @brief Non-blocking read check on the receive ring buffer.
 * @return Next character byte if available, or -1 if receive queue is empty.
 */
int usb_peek( void );

#ifdef __cplusplus
}
#endif

#endif // USB_H