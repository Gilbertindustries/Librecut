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
//HAL FILES
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
//---------------
#include "globalvar.h"

#ifdef __cplusplus
}
#endif

static FILE usb;

/* -------------------------------------------------------------------------
 * Keypad Key Scan Codes
 * ------------------------------------------------------------------------- */
#define LOAD_PAPER     0x40 
#define UNLOAD_PAPER   0x41
#define SET_ORIGIN_KEY 0x2e  // "Set Origin" / "Set Cut Location" Key Scan Code
#define STOP (1 << 0)

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

static void poll_keypad( void )
{
    int key = keypad_scan( );

    if( key < 0 )
        return;

    // --- 1. SET ORIGIN (CUT Button 0x2e) ---
    if( key == SET_ORIGIN_KEY )
    {
        beep(40);
        msleep(50);
        beep(40); 
        stepper_set_origin();
        g_jog_active = 0; // Exit jog mode
        lcd_show_temp_message( "Origin Set!", 1200 );
        return;
    }

    // --- 2. JOGGING MODE KEY HANDLING ---
    if( g_jog_active )
    {
        int cur_x, cur_y;
        stepper_get_pos( &cur_x, &cur_y ); // Get current X and Y coordinates

        switch( key )
        {
            case KEY_JOG_UP:
                stepper_move( cur_x - 100, cur_y );
                break;
            case KEY_JOG_DOWN:
                stepper_move( cur_x + 100, cur_y );
                break;
            case KEY_JOG_LEFT:
                stepper_move( cur_x, cur_y + 100 );
                break;
            case KEY_JOG_RIGHT:
                stepper_move( cur_x, cur_y - 100 );
                break;
            default:
                break;
        }
        return;
    }

    // --- 3. NORMAL IDLE KEY HANDLING ---
    switch( key )
    {
        case LOAD_PAPER:
            beep(60);
            stepper_load_paper( );
            break;

        case UNLOAD_PAPER:
            beep(60);
            stepper_unload_paper( );
            break;

        case KEY_JOG_UP:
        case KEY_JOG_DOWN:
        case KEY_JOG_LEFT:
        case KEY_JOG_RIGHT:
            g_jog_active = 1;
            beep(40);
            break;

        default:
            printf( "unknown key %02x\n", key );
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
    
    // --- Trigger startup LED animation ---
    keypad_boot_animation( );

    flash_init( );
    dial_init( );

    beeper_on( 1760 );
    msleep( 10 );
    beeper_off( );
    globalvar_init();
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
            dial_poll( );
            check_and_apply_dials( );
            
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