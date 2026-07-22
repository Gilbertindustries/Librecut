/*
 * hpgl.c
 *
 * Lightweight HPGL parser for Freecut with extended 256-byte buffer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

#include "hpgl.h"
#include "stepper.h"

// 256 bytes fits long coordinate chains without crashing AVR SRAM
#define HPGL_BUF_LEN 256

#ifndef STEPS_PER_MM
#define STEPS_PER_MM 40  
#endif

static char hpgl_buf[HPGL_BUF_LEN];
static uint16_t hpgl_buf_idx = 0;
static uint8_t pen_down = 0;

void hpgl_init( void )
{
    hpgl_buf_idx = 0;
    pen_down = 0;
}

static long parse_long( char **ptr )
{
    long result = 0;
    int sign = 1;

    while( **ptr == ',' || **ptr == ' ' || **ptr == '\t' ) (*ptr)++;

    if( **ptr == '-' ) {
        sign = -1;
        (*ptr)++;
    } else if( **ptr == '+' ) {
        (*ptr)++;
    }

    while( **ptr >= '0' && **ptr <= '9' ) {
        result = result * 10 + (**ptr - '0');
        (*ptr)++;
    }

    return result * sign;
}

static void process_coordinate_pairs( char *args, uint8_t draw )
{
    char *ptr = args;

    while( *ptr != '\0' )
    {
        while( *ptr == ',' || *ptr == ' ' || *ptr == '\t' ) ptr++;
        if( *ptr == '\0' ) break;

        long x_raw = parse_long( &ptr );
        
        while( *ptr == ',' || *ptr == ' ' || *ptr == '\t' ) ptr++;
        if( *ptr == '\0' ) break;

        long y_raw = parse_long( &ptr );

        long x_steps = (x_raw * STEPS_PER_MM) / 40;
        long y_steps = (y_raw * STEPS_PER_MM) / 40;

        if( x_steps < 0 ) x_steps = 0;
        if( y_steps < 0 ) y_steps = 0;
        if( x_steps > 65535 ) x_steps = 65535;
        if( y_steps > 65535 ) y_steps = 65535;

        if( draw )
            stepper_draw( (uint16_t)x_steps, (uint16_t)y_steps );
        else
            stepper_move( (uint16_t)x_steps, (uint16_t)y_steps );
    }
}

static void hpgl_execute_command( char *cmd )
{
    while( *cmd == ' ' || *cmd == '\t' ) cmd++;
    if( !*cmd ) return;

    char opcode[3] = { (char)toupper((unsigned char)cmd[0]), (char)toupper((unsigned char)cmd[1]), '\0' };
    char *args = cmd + 2;

    if( strcmp( opcode, "IN" ) == 0 )
    {
        pen_down = 0;
        stepper_pressure( 200 );
        stepper_move( 0, 0 );
    }
    else if( strcmp( opcode, "PU" ) == 0 )
    {
        pen_down = 0;
        if( *args != '\0' )
            process_coordinate_pairs( args, 0 );
    }
    else if( strcmp( opcode, "PD" ) == 0 )
    {
        pen_down = 1;
        if( *args != '\0' )
            process_coordinate_pairs( args, 1 );
    }
    else if( strcmp( opcode, "SP" ) == 0 )
    {
        long pen_num = parse_long( &args );
        uint16_t press_val = 200;
        if( pen_num == 0 )      press_val = 300;
        else if( pen_num == 1 ) press_val = 200;
        else if( pen_num == 2 ) press_val = 100;
        else                    press_val = 50;
        stepper_pressure( press_val );
    }
    else if( strcmp( opcode, "VS" ) == 0 )
    {
        long speed_val = parse_long( &args );
        long calc_speed = 100 - (speed_val * 2);
        if( calc_speed < 10 )  calc_speed = 10;
        if( calc_speed > 100 ) calc_speed = 100;
        
        stepper_speed( (uint16_t)calc_speed );
    }
}

void hpgl_process_char( char c )
{
    if( c == ';' || c == '\r' || c == '\n' )
    {
        hpgl_buf[hpgl_buf_idx] = '\0';
        if( hpgl_buf_idx > 0 )
        {
            hpgl_execute_command( hpgl_buf );
            hpgl_buf_idx = 0;
        }
    }
    else if( hpgl_buf_idx < (HPGL_BUF_LEN - 1) )
    {
        hpgl_buf[hpgl_buf_idx++] = c;
    }
}

void hpgl_process_string( const char *str )
{
    while( *str )
    {
        hpgl_process_char( *str++ );
    }
}