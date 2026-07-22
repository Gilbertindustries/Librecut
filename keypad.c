/*
 * keypad.c
 *
 * Driver for 16-column x 5-row keypad matrix and status LEDs.
 *
 * Hardware Interface Details:
 * ---------------------------
 * The 16 matrix columns are driven via a 16-bit serial-in/parallel-out shift register.
 * The 5 matrix rows are read simultaneously as digital inputs on PORTG (PG0..PG4).
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <inttypes.h>
#include <stdio.h>
#include "keypad.h"
#include "stepper.h"

/* -------------------------------------------------------------------------
 * Hardware Pin Masks & Definitions
 * ------------------------------------------------------------------------- */
#define STOP    (1 << 0)  // PD0: Stop Button input
#define LEDS    (1 << 5)  // PD5: LED Output Enable control line
#define DATA    (1 << 6)  // PD6: Shift Register Data bit
#define CLK     (1 << 7)  // PD7: Shift Register Clock bit
#define ROWS    (0x1F)    // PG0..PG4 mask (5 bits for 5 matrix rows)

#define MAX_COLS 16       // Total columns driven by shift register
#define MAX_ROWS 5        // Total input rows read by micro

/* Shift register LED bit mask for Load Material key */
#ifndef LED_LOAD_PAPER
#define LED_LOAD_PAPER (1 << 8) // Bit 8 maps to key B4
#endif

/* -------------------------------------------------------------------------
 * Low-Level Bit Manipulation Macros
 * ------------------------------------------------------------------------- */
#define leds_on()   do { PORTD &= ~LEDS; } while(0)
#define leds_off()  do { PORTD |=  LEDS; } while(0)

#define clk_h()     do { PORTD |=  CLK;  } while(0)
#define clk_l()     do { PORTD &= ~CLK;  } while(0)

#define data_h()    do { PORTD |=  DATA; } while(0)
#define data_l()    do { PORTD &= ~DATA; } while(0)

#define get_rows()  (~PING & ROWS)

/* -------------------------------------------------------------------------
 * Driver State Variables
 * ------------------------------------------------------------------------- */
uint8_t keypad_state[MAX_COLS]; 
uint8_t keypad_prev[MAX_COLS];  
uint16_t leds = 0;              

/* -------------------------------------------------------------------------
 * Private Helper Functions
 * ------------------------------------------------------------------------- */
static void keypad_write_cols( int16_t val )
{
    int i;
    for( i = 0; i < MAX_COLS; i++ )
    {
        if( val < 0 ) 
            data_h( );
        else
            data_l( );

        clk_h( );
        val <<= 1;
        clk_l( );
    }
}

/* -------------------------------------------------------------------------
 * Public API Functions
 * ------------------------------------------------------------------------- */
void keypad_set_leds( uint16_t mask )
{
    leds = mask;
    leds_off( );
    keypad_write_cols( (int16_t)~leds );
    leds_on( );
}

void keypad_leds_enable( uint8_t enable )
{
    if( enable )
        keypad_set_leds( 0xFFFF );
    else
        keypad_set_leds( 0x0000 );
}

void keypad_update_load_led( void )
{
    static uint8_t blink_ticks = 0;
    static uint8_t led_state = 0;

    // Only blink if material is NOT loaded
    if( !stepper_is_material_loaded() )
    {
        // 12 ticks at 25Hz = ~500ms ON / ~500ms OFF (1 Hz steady blink)
        if( ++blink_ticks >= 12 )
        {
            blink_ticks = 0;
            led_state = !led_state;

            if( led_state )
            {
                leds |= LED_LOAD_PAPER;   // Set bit in global state
            }
            else
            {
                leds &= ~LED_LOAD_PAPER;  // Clear bit in global state
            }

            keypad_set_leds( leds );     // Write full updated state
        }
    }
    else
    {
        // Turn off LED completely when material is loaded
        blink_ticks = 0;
        led_state = 0;
        if( leds & LED_LOAD_PAPER )
        {
            leds &= ~LED_LOAD_PAPER;
            keypad_set_leds( leds );
        }
    }
}

char keypad_stop_pressed( void )
{
    return !(PIND & STOP);
}

int keypad_scan( void )
{
    int row, col;
    int pressed = -1;

    leds_off( );    

    keypad_write_cols( (int16_t)~1 );  
    data_h( );    

    for( col = 0; col < MAX_COLS; col++ )
    {
        keypad_state[col] = get_rows( );
        clk_h( );
        clk_l( );
    }

    // Restore active LED state after matrix scan completes
    keypad_write_cols( (int16_t)~leds );
    leds_on( ); 

    for( col = 0; col < MAX_COLS; col++ )
    {
        uint8_t diff = keypad_state[col] ^ keypad_prev[col];

        if( diff )
        {
            for( row = 0; row < MAX_ROWS; row++ )
            {
                uint8_t mask = (uint8_t)(1 << row);

                if( diff & mask & keypad_state[col] )
                {
                    pressed = row * 16 + col;
                }
            }
        }
        
        keypad_prev[col] = keypad_state[col];
    }

    return pressed;
}

void keypad_init( void )
{
    clk_l( );
    leds_off( );

    DDRD |= (LEDS | DATA | CLK); 
    PORTD |= STOP;

    DDRG &= ~ROWS;  
    PORTG |= ROWS;  
}