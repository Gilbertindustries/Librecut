/*
 * usb.c
 *
 * Driver for USB serial communication routed via an onboard FTDI USB-to-UART chip.
 *
 * Hardware Interface Details:
 * ---------------------------
 * Uses AVR Hardware USART1 interface:
 * - Data transmit buffer relies on USART Data Register Empty ISR (`USART1_UDRE_vect`)
 * - Data receive buffer relies on USART Receive Complete ISR (`USART1_RX_vect`)
 *
 * Buffer Mechanics:
 * -----------------
 * Implements lightweight lock-free circular ring buffers for RX and TX queues using 
 * power-of-two modulus arithmetic. Head and tail pointers utilize `uint8_t` wraparound.
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
 * aromatic or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public
 * License for more details.
 */

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/wdt.h>
#include <inttypes.h>
#include <stdio.h>
#include "usb.h"

// Define clock frequency safety fallback if FCLK is not defined by build environment
#ifndef FCLK
#ifdef F_CPU
#define FCLK F_CPU
#else
#define FCLK 16000000UL // Default 16 MHz
#endif
#endif

/* -------------------------------------------------------------------------
 * Ring Buffer Sizing Parameters (Must be exact powers of 2 for fast modulo)
 * ------------------------------------------------------------------------- */
#define USB_INBUF_LEN  128  // Receive queue size (128 Bytes)
#define USB_OUTBUF_LEN 32   // Transmit queue size (32 Bytes)

static uint8_t usb_tx_buf[USB_OUTBUF_LEN];
static uint8_t usb_rx_buf[USB_INBUF_LEN];

// Volatile pointers required for shared access between main loop and ISR handlers
volatile static uint8_t usb_tx_head = 0;
volatile static uint8_t usb_tx_tail = 0;
volatile static uint8_t usb_rx_head = 0;
volatile static uint8_t usb_rx_tail = 0;

// Declare I/O stream descriptor for standard library integration
FILE usb;

/* -------------------------------------------------------------------------
 * Interrupt Service Routines
 * ------------------------------------------------------------------------- */

/**
 * @brief Data Register Empty ISR for USART1 (Fires when UDR1 is ready for new byte).
 * Transmits next byte from TX ring buffer or disables interrupt if queue is empty.
 */
ISR( USART1_UDRE_vect ) 
{
    if( usb_tx_tail == usb_tx_head )
    {
        // Queue empty: Disable UDRE interrupt to prevent infinite interrupt loop
        UCSR1B &= ~(1 << UDRIE1);
    }
    else
    {
        // Shift next character out to UDR1 transmit hardware register
        UDR1 = usb_tx_buf[usb_tx_tail++ % USB_OUTBUF_LEN];
    }
}

/**
 * @brief Receive Complete ISR for USART1 (Fires when a character is received).
 * Appends hardware byte to RX ring buffer if capacity remains.
 */
ISR( USART1_RX_vect )
{
    char c = UDR1; // Read byte immediately to clear hardware buffer flag

    // Check if RX ring buffer is full (Drop byte if overflow condition occurs)
    if( (uint8_t)(usb_rx_head - usb_rx_tail) >= USB_INBUF_LEN )
        return;

    usb_rx_buf[usb_rx_head++ % USB_INBUF_LEN] = (uint8_t)c;
}

/* -------------------------------------------------------------------------
 * Public API Functions
 * ------------------------------------------------------------------------- */

/**
 * @brief Computes UBRR register divider assuming double speed mode (U2X1 = 1).
 * Formula: UBRR = (FCLK / (8 * Baud)) - 1
 * @param baud Target communication speed in bits per second.
 */
void usb_set_baud( long baud )
{
    // Integer arithmetic with rounding offset: (FCLK + baud*4) / (baud*8) - 1
    uint16_t div = (uint16_t)((FCLK + (baud * 4L)) / (baud * 8L) - 1);

    UBRR1H = (uint8_t)(div >> 8);
    UBRR1L = (uint8_t)div;
}

/**
 * @brief Pushes character byte into transmit queue and enables UDRE interrupt.
 * Converts newlines ('\n') to CRLF sequence ('\r\n').
 */
int usb_putchar( char c, FILE *stream ) 
{
    (void)stream;

    // Convert Unix line feed to Carriage Return + Line Feed sequence
    if( c == '\n' )
        usb_putchar( '\r', stream );

    // Block if transmit buffer is full, service watchdog timer while waiting
    while( (uint8_t)(usb_tx_head - usb_tx_tail) == USB_OUTBUF_LEN )
        wdt_reset( );

    // Store character byte in buffer
    usb_tx_buf[usb_tx_head++ % USB_OUTBUF_LEN] = (uint8_t)c;

    // Enable Data Register Empty Interrupt to trigger transmission in background
    UCSR1B |= (1 << UDRIE1);
    
    return 0;
}

/**
 * @brief Non-blocking read attempt from RX circular queue.
 * @return Read character if present, or -1 if queue contains no data.
 */
int usb_peek( void )
{
    if( usb_rx_head == usb_rx_tail )
        return -1;

    return usb_rx_buf[usb_rx_tail++ % USB_INBUF_LEN];
}

/**
 * @brief Blocking character read from receive queue.
 */
int usb_getchar( FILE *stream )
{
    (void)stream;
    int c;

    // Poll until data is available, resetting watchdog timer to avoid reset
    while( (c = usb_peek()) < 0 )
        wdt_reset( );

    return c;
}

/**
 * @brief Configures USART1 baud rate, framing (8N1), and hardware interrupt masks.
 */
void usb_init( void ) 
{
    // Bind stream callbacks for standard C library support
    fdev_setup_stream(&usb, usb_putchar, usb_getchar, _FDEV_SETUP_RW);

    // Enable 2X Double Speed Mode
    UCSR1A = (1 << U2X1);

    // Set 115200 Baud default speed
    usb_set_baud( 115200L );

    // Enable RX Complete Interrupt, Receiver, UDRE Interrupt, Transmitter
    UCSR1B = (1 << RXCIE1) | (1 << RXEN1) | (1 << UDRIE1) | (1 << TXEN1);
}