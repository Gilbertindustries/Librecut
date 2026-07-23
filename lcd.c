/*
 * lcd.c
 * Driver for LCD module based on standard Hitachi HD44780, operating in 4-bit mode.
 *
 * Pinout Wiring:
 * --------------
 * Pin | Func  | AVR Pin | Description
 * ----+-------+---------+------------------------------------
 * 3   | RS    | PF5     | Register Select (0=Command, 1=Data)
 * 4   | R/!W  | PF6     | Read/Write (0=Write, 1=Read)
 * 5   | E     | PF7     | Enable Strobe / Clock Pin
 * 6   | D4    | PE4     | Data Bit 4
 * 7   | D5    | PE5     | Data Bit 5
 * 8   | D6    | PE6     | Data Bit 6
 * 9   | D7    | PE7     | Data Bit 7 (also Busy Flag on read)
 * 10  | Light | PF4     | Backlight Control (0=ON, 1=OFF)
 *
 * Display Addressing Memory Mapping:
 * ----------------------------------
 * The physical display is a 16x1 character layout, but internally it is wired
 * as a 40x2 display line layout:
 *
 * Line 0 (Chars 0..7):   DDRAM Address 00..07
 * Line 1 (Chars 8..15):  DDRAM Address 40..47
 *
 * Memory layout:
 * [00][01][02][03][04][05][06][07] | [40][41][42][43][44][45][46][47]
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

#include <avr/io.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include "lcd.h"
#include "timer.h"
#include "stepper.h"
#include "globalvar.h"

/* -------------------------------------------------------------------------
 * Bit Masks & Control Line Macros
 * ------------------------------------------------------------------------- */
#define BACKLIGHT (1 << 4)    // PF4: Backlight Enable Pin
#define RS        (1 << 5)    // PF5: Register Select
#define RW        (1 << 6)    // PF6: Read/Write Select
#define E         (1 << 7)    // PF7: Enable Strobe
#define DATA      (0xF0)      // PE4..PE7: Upper 4-bit data bus

// Set Read/Write line HIGH (Read) or LOW (Write)
#define rw_h()  do { PORTF |=  RW; } while(0)
#define rw_l()  do { PORTF &= ~RW; } while(0)

// Pulse Enable line HIGH or LOW (Latch clock for HD44780)
#define e_h()   do { PORTF |=  E;  } while(0)
#define e_l()   do { PORTF &= ~E;  } while(0)

// Set Register Select HIGH (Data) or LOW (Instruction)
#define rs_h()  do { PORTF |=  RS; } while(0)
#define rs_l()  do { PORTF &= ~RS; } while(0)

/* -------------------------------------------------------------------------
 * Global / File Scope Variables
 * ------------------------------------------------------------------------- */
uint8_t current_pos = 0; // Tracks virtual column index (0 to 15)

// Declare stdout stream without static C++ incompatible macro initializer
FILE lcd;

/* -------------------------------------------------------------------------
 * Low-Level Nibble Reading / Writing Logic
 * ------------------------------------------------------------------------- */

/**
 * @brief Reads an 8-bit byte from the LCD controller by reconfiguring 
 * PORTE as inputs and clocking two 4-bit nibbles.
 * @return 8-bit value containing HD44780 register contents (bit 7 = Busy Flag).
 */
uint8_t lcd_read( void )
{
    uint8_t val;

    PORTE |= DATA;   // Enable internal pull-up resistors on data lines
    DDRE &= ~DATA;   // Configure PE4..PE7 as inputs
    
    rw_h( );         // Switch LCD interface to READ mode
    
    // Read High Nibble (D7..D4)
    e_h( );          
    e_h( );          // Dummy clock cycles to meet HD44780 enable pulse time (t_PWE)
    val = PINE & DATA; // Read MSB nibble
    e_l( );

    // Read Low Nibble (D3..D0 shifted into D7..D4 pins)
    e_h( );
    e_h( );
    val |= (PINE >> 4); // Shift LSB nibble to bottom 4 bits
    e_l( );

    // Restore bus direction to default WRITE mode
    rw_l( );         
    DDRE |= DATA;    // Reconfigure PE4..PE7 as outputs
    
    return val;
}

/**
 * @brief Enables display backlight.
 */
void lcd_backlight_on( void )
{
    PORTF &= ~BACKLIGHT; // Active LOW output
}

/**
 * @brief Disables display backlight.
 */
void lcd_backlight_off( void )
{
    PORTF |= BACKLIGHT;  // Active LOW output
}

/**
 * @brief Polls bit 7 of the LCD controller until the Busy Flag (BF) clears.
 * @return 0 if ready, -1 if operation timed out.
 */
int lcd_wait_ready( void )
{
    int i;

    // Poll status register until Bit 7 (Busy Flag) becomes 0
    for( i = 0; i < 10000; i++ )
    {
        if( !(lcd_read() & 0x80) )
            return 0;
    }
    return -1; // Controller did not respond in time
}

/**
 * @brief Outputs a single 4-bit nibble on PE4..PE7 and strobes Enable.
 * @param val Contains the nibble data in bits 7..4.
 */
void lcd_write_nibble( uint8_t val )
{
    PORTE = (PORTE & ~DATA) | (val & DATA);
    e_h( );
    e_l( ); // Latch data on falling edge
}

/**
 * @brief Sends an 8-bit instruction/command byte to the HD44780 controller.
 * @param val Command opcode.
 */
void lcd_write_control( uint8_t val )
{
    lcd_write_nibble( val );        // Send MSB 4 bits
    lcd_write_nibble( val << 4 );   // Send LSB 4 bits
    lcd_wait_ready( );              // Wait for execution to finish
}

/* -------------------------------------------------------------------------
 * Display Manipulation & Standard I/O Functions
 * ------------------------------------------------------------------------- */

/**
 * @brief Clears display DDRAM and rests cursor to position 0.
 */
void lcd_clear( void )
{
    lcd_write_control( 0x01 );
    msleep( 2 ); // Clear screen requires ~1.52 ms execution time
    current_pos = 0;
}

/**
 * @brief Sets cursor character position from 0 to 15.
 * Performs DDRAM address translation for split 16x1 screens.
 * @param pos Column position (0..15)
 */
void lcd_pos( uint8_t pos )
{
    if( pos >= 16 )
        pos = 0;

    current_pos = pos;

    // Convert display position 8..15 to Line 2 DDRAM memory block (0x40..0x47)
    if( pos >= 8 )
        pos += 32; // Offset: 8 + 32 = 40 (0x28)

    // Command 0x80 = "Set DDRAM Address"
    lcd_write_control( 0x80 + pos );
}

/**
 * @brief Writes an ASCII character to the current LCD cursor location.
 * @param c Character byte to output.
 * @param stream Target FILE stream descriptor.
 * @return 1 on success.
 */
int lcd_putchar( char c, FILE *stream )
{
    (void)stream;

    // Handle newline: Jump directly to Line 2 (Position 8)
    if( c == '\n' )
    {
        lcd_pos( 8 );
        return 1;
    }

    // Handle carriage return: Move cursor back to start (Position 0)
    if( c == '\r' )
    {
        lcd_pos( 0 );
        return 1;
    }

    // Ignore extra characters if line exceeds 16 total characters
    if( current_pos >= 16 ) 
        return 1;

    rs_h(); // Select Data Register (RS = 1)
    
    lcd_write_nibble( (uint8_t)c );        // Send high 4 bits
    lcd_write_nibble( (uint8_t)(c << 4) ); // Send low 4 bits
    
    rs_l(); // Reset to Control Register (RS = 0)
    
    lcd_wait_ready( );

    // Auto-advance cursor and handle boundary transition from char 7 to char 8
    if( ++current_pos == 8 )
        lcd_pos( current_pos );

    return 1;
}

/**
 * @brief Writes a null-terminated string to the LCD screen.
 * @param str Pointer to character string.
 */
void lcd_puts( const char *str )
{
    while( *str )
    {
        lcd_putchar( *str++, NULL );
    }
}

/**
 * @brief Checks stepper load status and updates display text dynamically.
 */
/**
 * @brief Checks stepper load status and updates display text dynamically.
 */
/**
 * @brief Checks stepper load status and updates display text dynamically.
 */
static uint16_t dial_display_timer = 0; // Tracks display hold time in 25Hz ticks

void lcd_show_dial_override( int spd, int prs, int siz )
{
    lcd_clear();
    // Use &lcd stream descriptor to print directly to display
    fprintf( &lcd, "S:%d P:%d Z:%d", spd, prs, siz );
    
    // Hold dial message for 50 ticks (~2 seconds at 25Hz)
    dial_display_timer = 50; 
}

void lcd_update_status( void )
{
    static uint8_t prev_loaded_state = 0xFF;

    // 1. If user recently moved a dial, decrement timer and pause normal status/scroll
    if( dial_display_timer > 0 )
    {
        dial_display_timer--;
        // Reset state tracker so scrolling resumes cleanly after timer expires
        prev_loaded_state = 0xFF; 
        return;
    }

    // Don't overwrite screen during homing
    if( stepper_get_state() == HOME1 || stepper_get_state() == HOME2 )
    {
        prev_loaded_state = 0xFF;
        return;
    }

    // 2. Check if the machine is currently jogging
    if( g_jog_active == 1) // Replace JOGGING with your actual state enum/macro
    {
        if( prev_loaded_state != 2 ) // Using state '2' to track the Jogger display state
        {
            lcd_clear();
            prev_loaded_state = 2;
        }

        lcd_scroll_text( "Origin mode active. Press CUT to set origin. Press STOP to exit... " );
        return;
    }

    // 3. Normal Material Status Logic
    uint8_t current_loaded_state = stepper_is_material_loaded();

    if( !current_loaded_state )
    {
        if( prev_loaded_state != 0 )
        {
            lcd_clear();
            lcd_puts( "Load Material" );
            prev_loaded_state = 0;
        }
    }
    else
    {
        if( prev_loaded_state != 1 )
        {
            lcd_clear();
            prev_loaded_state = 1;
        }

        lcd_scroll_text( "Ready to Cut!   Hit flashing key to unload material... " );
    }
}

/**
 * @brief Initializes hardware lines, configures standard output FILE stream, 
 * and performs the HD44780 4-bit initialization sequence.
 */
 
void lcd_init( void )
{
    // Bind stdout stream descriptor for standard C formatting compatibility
    fdev_setup_stream(&lcd, lcd_putchar, NULL, _FDEV_SETUP_WRITE);

    // Initial state: Control lines LOW (RS=0, E=0, RW=0)
    PORTF &= ~(RW | E | RS);
    
    // Set PORTF control lines as outputs
    DDRF |= (RW | E | RS | BACKLIGHT);
    
    // Set PORTE data bus as outputs
    DDRE |= DATA;
    
    /* ---------------------------------------------------------------------
     * HD44780 Datasheet 4-Bit Initialization Protocol
     * --------------------------------------------------------------------- */
    msleep( 15 );              // Power-on delay > 15 ms
    lcd_write_nibble( 0x30 );  // Step 1: Soft-reset to 8-bit mode
    msleep( 5 );               // Wait > 4.1 ms
    
    lcd_write_nibble( 0x30 );  // Step 2: Second reset attempt
    usleep( 100 );             // Wait > 100 us
    
    lcd_write_nibble( 0x30 );  // Step 3: Third reset attempt
    usleep( 100 );             
    
    lcd_write_nibble( 0x20 );  // Step 4: Configure interface to 4-bit operation
    usleep( 100 );             

    // Controller is now operating in 4-bit mode; busy flag detection becomes valid
    lcd_write_control( 0x28 ); // 4-bit mode, 2-line display, 5x8 font matrix
    lcd_write_control( 0x0C ); // Display ON, Cursor OFF, Blink OFF
    lcd_write_control( 0x01 ); // Clear display and reset DDRAM address to 0
    
    current_pos = 0;           // Reset local position tracker
    lcd_backlight_on( );
} 

/* -------------------------------------------------------------------------
 * Debugging Helper Functions
 * ------------------------------------------------------------------------- */

/**
 * @brief Converts a 4-bit nibble value to hex ASCII and prints to LCD.
 * @param x 4-bit value (0..15).
 */
void lcd_putnibble( uint8_t x )
{
    if( x < 10 )
        lcd_putchar( '0' + x, NULL );
    else
        lcd_putchar( 'a' + (x - 10), NULL );
}

/**
 * @brief Converts an 8-bit byte to 2-digit hex ASCII and prints to LCD.
 * @param x Byte value.
 */
void lcd_puthex( uint8_t x )
{
    lcd_putnibble( x >> 4 );
    lcd_putnibble( x & 0x0F );
}


void lcd_show_temp_message( const char *str, uint16_t delay_ms )
{
    lcd_clear();
    lcd_puts( str );
    msleep( delay_ms );
    lcd_clear();
}


/**
 * @brief Non-blocking scroll for long strings across a 16-character LCD screen.
 * Call this periodically (e.g., inside a 25Hz loop).
 * @param str The long string to scroll across the screen.
 */
void lcd_scroll_text( const char *str )
{
    static uint16_t scroll_pos = 0;
    static uint8_t ticks = 0;
    uint8_t len = strlen( str );

    // Step the scroll position every 8 ticks (~320ms delay at 25Hz)
    if( ++ticks >= 8 )
    {
        ticks = 0;
        
        lcd_clear();
        
        // Print 16-character window starting from scroll_pos
        for( uint8_t i = 0; i < 16; i++ )
        {
            uint16_t idx = (scroll_pos + i) % (len + 4); // +4 adds spacing before loop resets
            if( idx < len )
            {
                lcd_putchar( str[idx], NULL );
            }
            else
            {
                lcd_putchar( ' ', NULL ); // Print padding spaces at end of string
            }
        }

        // Advance starting index for the next frame
        scroll_pos++;
        if( scroll_pos >= (len + 4) )
        {
            scroll_pos = 0; // Wrap back to start
        }
    }
}