/*
 * main.c / Librecut.ino
 * Librecut Gilbert Industries
 * Based on Arlet's Freecut
 * Freecut firmware, main program
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <inttypes.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "usb.h"
#include "keypad.h"
#include "lcd.h"
#include "timer.h"
#include "stepper.h"
#include "cli.h"
#include "flash.h"
#include "spooler.h"
#include "version.h"
#include "dial.h"

#ifdef __cplusplus
}
#endif

static FILE usb;

#define LOAD_PAPER   0x40 
#define UNLOAD_PAPER 0x4d

void poll_keypad( void )
{
    int key = keypad_scan( );

    switch( key )
    {
        case LOAD_PAPER: 
            // 1. Trigger home sequence when loading paper
            stepper_home(); 
            stepper_load_paper(); 
            break;

        case UNLOAD_PAPER:
            stepper_unload_paper(); 
            break;

        default:
            if( key >= 0 )
                printf( "# unknown key %02x\n", key );
    }
}

int main( void )
{
    wdt_disable();

    fdev_setup_stream(&usb, usb_putchar, usb_getchar, _FDEV_SETUP_RW);

    usb_init( );
    timer_init( );
    stepper_init( );
    sei( );
    lcd_init( );
    keypad_init( );
    flash_init( );
    dial_init( );

    beeper_on( 1760 );
    msleep( 10 );
    beeper_off( );

    stdout = &usb;
    stdin  = &usb;

    printf( "Freecut v" VERSION "\n\n" );
    
    // 2. Initial homing on startup
    stepper_home();

    wdt_enable( WDTO_2S );

    while( 1 )
    {
        cli_poll( );
        wdt_reset( );

        if( flag_25Hz )
        {
            flag_25Hz = 0;
            dial_poll( );
            keypad_update_load_led( );
            poll_keypad( );

            // 3. Only refresh status screen when NOT active homing
            if( stepper_get_state() == READY ) 
            {
                lcd_update_status( ); 
            }
        }

        if( flag_Hz )
        {
            flag_Hz = 0;
        }
    }
}