/*
 * flash.c
 *
 * Driver for serial dataflash AT45DB041B on main board, attached as follows:
 *
 * Pin  |  Func | AVR
 *------+-------+---------
 * 1    |  SI   | PB7
 * 2    |  SCK  | PB1 
 * 4    |  !CS  | PB4 
 * 8    |  SO   | PB0 
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
 *
 */

#include <avr/io.h>
#include <stdio.h>
#include <inttypes.h>
#include "flash.h"
#include "usb.h"

#define AT_BUF1_WRITE   0x84  // Write data to SRAM Buffer 1
#define AT_BUF1_TO_MM   0x83  // Program SRAM Buffer 1 into Main Memory Page (with built-in erase)
#define MISO (1 << 0)
#define SCK  (1 << 1)
#define CS   (1 << 4)
#define MOSI (1 << 7)

#define cs_low( )    do { PORTB &= ~CS; } while(0)
#define cs_high( )   do { PORTB |=  CS; } while(0)
#define mosi_low( )  do { PORTB &= ~MOSI; } while(0)
#define mosi_high( ) do { PORTB |=  MOSI; } while(0)
#define sck_low( )   do { PORTB &= ~SCK; } while(0)
#define sck_high( )  do { PORTB |=  SCK; } while(0)
#define get_miso( )  ((PINB & MISO) ? 1 : 0)

#define AT_STATUS    0xd7    // status byte 
#define AT_CAR       0xe8    // continuous array read

/*
 * Helper functions exported for spooler.c
 */
void flash_cs_low( void )  { cs_low(); }
void flash_cs_high( void ) { cs_high(); }

/*
 * write a single byte to flash chip via software SPI bit-banging
 */
void flash_write_byte( uint8_t data )
{
    uint8_t i;

    for( i = 0; i < 8; i++ )
    {
        if( data & 0x80 )
            mosi_high( );
        else
            mosi_low( );
            
        sck_high( );
        data <<= 1;
        sck_low( );
    }
}

/*
 * read a single byte from flash chip via software SPI bit-banging
 */
uint8_t flash_read_byte( void )
{
    uint8_t data = 0;
    uint8_t i;

    for( i = 0; i < 8; i++ )
    {
        sck_high( );
        data = (uint8_t)((data << 1) | get_miso( ));
        sck_low( );
    }
    return data;
}

/*
 * provide 'count' dummy clock cycles
 */
void flash_clocks( uint8_t count )
{
    while( count-- > 0 )
    {
        sck_high( );
        sck_low( );
    }
}

/*
 * read the Flash status byte
 */
uint8_t flash_read_status( void )
{
    uint8_t status;

    cs_low( );
    flash_write_byte( AT_STATUS );
    status = flash_read_byte( );
    cs_high( );
    return status;
}

/*
 * write a command, consisting of 8-bit command and 24-bit address.
 */
void flash_write_cmd( uint8_t cmd, uint32_t addr )
{
    cs_low( );
    flash_write_byte( cmd );
    flash_write_byte( (uint8_t)(addr >> 16) );
    flash_write_byte( (uint8_t)(addr >> 8) );
    flash_write_byte( (uint8_t)(addr) );
}

/*
 * simple test: dump all contents of built-in cartridge
 */
void flash_test( void )
{
    uint8_t i = 0;
    int32_t size = 2048L * 264L;

    flash_write_cmd( AT_CAR, 0 );
    flash_clocks( 32 );
    
    while( size-- > 0 && usb_peek() != 3 )
    {
        printf( "%02x%c", flash_read_byte(), (++i % 16) ? ' ' : '\n' );
    }
    cs_high( );
}

/**
 * @brief Writes a 264-byte page to the AT45DB041B DataFlash chip.
 * @param page Page address (0 to 2047)
 * @param buffer Pointer to a 264-byte array containing data to write
 */
void flash_write_page( uint16_t page, uint8_t *buffer )
{
    uint16_t i;
    
    // 1. Write the dataset into Flash Buffer 1
    cs_low();
    flash_write_byte( AT_BUF1_WRITE );
    flash_write_byte( 0x00 ); // Starting byte offset in buffer (Byte 0)
    flash_write_byte( 0x00 );
    flash_write_byte( 0x00 );

    for( i = 0; i < 264; i++ )
    {
        flash_write_byte( buffer[i] );
    }
    cs_high();

    // 2. Commit Buffer 1 into Main Memory Page (Built-in page erase + write)
    cs_low();
    flash_write_byte( AT_BUF1_TO_MM );
    flash_write_byte( (uint8_t)(page >> 7) );        // Page address bits 10..3
    flash_write_byte( (uint8_t)(page << 1) );        // Page address bits 2..0
    flash_write_byte( 0x00 );                        // Don't care bits
    cs_high();

    // 3. Wait for the flash chip to finish writing internally
    while( !(flash_read_status() & 0x80) ); // Bit 7 of status register indicates READY (1) or BUSY (0)
}

void flash_init( void )
{
    DDRB |= MOSI | SCK | CS;
    cs_high( );
    flash_read_status( );
}