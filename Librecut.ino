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

/* -------------------------------------------------------------------------
 * Keypad Key Scan Codes
 * ------------------------------------------------------------------------- */
#define LOAD_PAPER     0x40 
#define UNLOAD_PAPER   0x41

/* -------------------------------------------------------------------------
 * Directional Jog Keys Mapped:
 * DOWN  -> Carriage Right (+X)
 * UP    -> Carriage Left  (-X)
 * LEFT  -> Roller/Paper Up    (+Y)
 * RIGHT -> Roller/Paper Down  (-Y)
 * ------------------------------------------------------------------------- */
#define KEY_JOG_UP     0x3e
#define KEY_JOG_DOWN   0x4e
#define KEY_JOG_LEFT   0x1e
#define KEY_JOG_RIGHT  0x0e

void beep(int ms){
    beeper_on( 1760 );
    msleep( ms );
    beeper_off( );
}

void poll_keypad( void )
{
    int key = keypad_scan();
    if (key < 0) return;

    // Only allow jogging when the motor system is idle
    if (stepper_get_state() != READY) return;

    int cur_x, cur_y;
    stepper_get_pos(&cur_x, &cur_y);

    switch( key )
    {
        /* --- Toolhead Carriage (X Axis) --- */
        case KEY_JOG_DOWN:
            beep(20);
            stepper_speed(150);
            stepper_move(cur_x + 50, cur_y); // DOWN = Carriage Right (+X)
            break;

        case KEY_JOG_UP:
            beep(20);
            stepper_speed(150);
            stepper_move(cur_x - 50, cur_y); // UP = Carriage Left (-X)
            break;

        /* --- Roller / Paper Feed (Y Axis) --- */
        case KEY_JOG_LEFT:
            beep(20);
            stepper_speed(150);
            stepper_move(cur_x, cur_y + 50); // LEFT = Roller Up (+Y)
            break;

        case KEY_JOG_RIGHT:
            beep(20);
            stepper_speed(150);
            stepper_move(cur_x, cur_y - 50); // RIGHT = Roller Down (-Y)
            break;

        case LOAD_PAPER: 
            beep(20);
            stepper_home(); 
            stepper_load_paper(); 
            break;

        case UNLOAD_PAPER:
            beep(20);
            stepper_unload_paper(); 
            break;

        default:
            printf("# unknown key %02x\n", key);
            break;
    }
}

void check_and_apply_dials( void )
{
    static int last_speed = -1;
    static int last_press = -1;
    static int last_size  = -1;

    int current_speed_step = dial_get_speed();
    int current_press_step = dial_get_pressure();
    int current_size_step  = dial_get_size();

    // Check if any dial changed position
    if( current_speed_step != last_speed || 
        current_press_step != last_press || 
        current_size_step  != last_size )
    {
        last_speed = current_speed_step;
        last_press = current_press_step;
        last_size  = current_size_step;

        // Apply updated hardware values
        beep(20);
        stepper_speed( dial_get_mapped_speed() );
        stepper_pressure( dial_get_mapped_pressure() );

        // Pause scrolling and show dial settings (1-indexed for display)
        lcd_show_dial_override( current_speed_step + 1, 
                               current_press_step + 1, 
                               current_size_step  + 1 );
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

    printf( "Librecut v" VERSION "\n\n" );
    
    // Initial homing sequence on boot
    lcd_show_temp_message( "Librecut v" VERSION "", 1500 );
    stepper_home();

    wdt_enable( WDTO_2S );

    while( 1 )
    {
        cli_poll( );
        wdt_reset( );

        if( flag_25Hz )
        {
            flag_25Hz = 0;
            dial_poll( );              // Reads the ADC channels
            check_and_apply_dials( );  // <-- Added: Applies updated dial settings to motors/solenoid
            
            keypad_update_load_led( );
            keypad_update_unload_led( );
            poll_keypad( );

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