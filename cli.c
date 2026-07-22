/*
 * cli.c
 *
 * Command line interface for streaming motion commands, utility controls,
 * and board diagnostics over USB UART.
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

#include <avr/wdt.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <setjmp.h>
#include <math.h>

#include "timer.h"
#include "cli.h"
#include "usb.h"
#include "stepper.h"
#include "flash.h"
#include "version.h"
#include "keypad.h"
#include "hpgl.h"
#include "spooler.h"
#include "lcd.h"

#define BUFLEN 100 
#define MAX_ARGS 10

static char buf[BUFLEN + 1];
static char *argv[MAX_ARGS+1];
static uint8_t buf_len = 0;
static uint8_t argc;
static uint8_t curtok;
static char echo = 0;
static jmp_buf cmd_error;

enum
{
    C_EOL = 0, 
    C_BAD,
    C_NUM,
    C_MOVE,
    C_DRAW,
    C_VERSION,
    C_SPEED,
    C_PRESS,
    C_CURVE,
    C_FLASH,
    C_FLASHWRITE,
    C_RESET,
    C_LEDS,
    C_LED,
    C_HPGL,
    C_PROGRESS,
    C_DONE
};

static struct token
{
    char *lex;
    uint8_t class;
    long val;
} token;

typedef struct
{
    char str[10];
    uint8_t class;
} keyword_t;

static const keyword_t keyword_list[] PROGMEM = 
{
    { "version",    C_VERSION },
    { "move",       C_MOVE },
    { "draw",       C_DRAW },
    { "speed",      C_SPEED },
    { "curve",      C_CURVE },
    { "press",      C_PRESS },
    { "pressure",   C_PRESS },
    { "flash",      C_FLASH },
    { "flashwrite", C_FLASHWRITE },
    { "reset",      C_RESET },
    { "leds",       C_LEDS },
    { "lights",     C_LEDS },
    { "led",        C_LED },
    { "hpgl",       C_HPGL },
    { "HPGL",       C_HPGL },
    { "progress",   C_PROGRESS },
    { "done",       C_DONE },
};
#define MAX_KEYWORDS (sizeof(keyword_list) / sizeof(keyword_list[0]))

static uint8_t find_keyword( char *str, uint8_t *class_out )
{
    unsigned i;
    for( i = 0; i < MAX_KEYWORDS; i++ )
    {
        if( !strcmp_P(str, (PGM_P)&keyword_list[i].str) )
        {
            *class_out = pgm_read_byte(&keyword_list[i].class);
            return 1;
        }
    }
    return 0;
}

static uint8_t get_token( void )
{
    char *lex = argv[curtok];
    uint8_t class;

    token.lex = lex;
    if( curtok >= argc )
    {
        token.class = C_EOL;
        return C_EOL;
    }
    curtok++;

    if( find_keyword( lex, &class ) )
    {
        // Keyword matched
    }
    else if( lex[0] == '-' || isdigit((unsigned char)lex[0]) )
    {
        token.val = strtol( lex, NULL, 10 ); 
        class = C_NUM;
    }
    else
    {
        class = C_BAD;
    }

    token.class = class;
    return( class );
}

static void expect_token( uint8_t class, const char *what )
{
    if( get_token() == class )
        return;
    printf( "expected %s, got '%s'\n", what, token.lex );
    longjmp( cmd_error, 1 );
}

static long parse_num( void )
{
    expect_token( C_NUM, "number" );
    return( token.val );
}

static char split_line( char *line, char **argv )
{
    uint8_t argc_count = 0;
    char c;

    for( argc_count = 0; argc_count < MAX_ARGS; argc_count++ )
    {
        while( *line == ' ' )
            *line++ = 0;
        if( !*line )
            break;
        argv[argc_count] = line;
        while( (c = *line) != 0 && c != ' ' )
            line++;
    }
    argv[argc_count] = "";
    return( argc_count );
}

static void parse_progress( void )
{
    int current = (int)parse_num();
    int total   = (int)parse_num();

    lcd_clear();
    fprintf( &lcd, "[%d/%d]", current, total );
}

void version( void )
{
    printf( "Librecut version " VERSION "\n");
}

static void parse_single_led( void )
{
    char row_char = 0;
    int col = 0;

    if( curtok < argc )
    {
        char *arg = argv[curtok++];
        
        row_char = toupper((unsigned char)arg[0]);
        if( arg[1] != '\0' )
        {
            col = atoi(&arg[1]);
        }
        else if( curtok < argc )
        {
            col = parse_num();
        }
    }

    if( (row_char != 'A' && row_char != 'B') || col < 1 || col > 5 )
    {
        printf( "Usage: led <A|B> <1-5>  (e.g., 'led A1' or 'led B3')\n" );
        return;
    }

    uint8_t bit_index = (row_char == 'A' ? 0 : 5) + (col - 1);
    uint16_t led_mask = (1 << bit_index);

    keypad_set_leds( led_mask );
    printf( "LED %c%d (Bit %d) turned ON\n", row_char, col, bit_index );
}

static void parse_leds( void )
{
    uint8_t state = 1;

    if( curtok < argc )
    {
        state = (uint8_t)parse_num();
    }
    
    keypad_leds_enable( state );

    if( state )
        printf( "Keypad LEDs ON\n" );
    else
        printf( "Keypad LEDs OFF\n" );
}

static void parse_speed( void )
{
    stepper_speed( parse_num() );
}

static void parse_move( void )
{
    int16_t x = (int16_t)parse_num( );
    int16_t y = (int16_t)parse_num( );

    if( x < 0 )
        x = 0;
    stepper_move( x, y );
}

static void parse_draw( void )
{
    uint16_t x = parse_num( );
    uint16_t y = parse_num( );

    stepper_draw( x, y );
}

static void bezier_prep( int *k, int *p )
{
    k[0] =   -p[0] + 3*p[1] - 3*p[2] + p[3];
    k[1] =  3*p[0] - 6*p[1] + 3*p[2];
    k[2] = -3*p[0] + 3*p[1];
    k[3] =    p[0];
}

static int bezier_eval( int *p, float t )
{
    float s;
    
    s  = p[0]; s *= t;
    s += p[1]; s *= t;
    s += p[2]; s *= t;
    s += p[3];
    
    return (int) (s + 0.5f);
}

static void parse_curve( void )
{
    int i, x, y;
    int ox, oy;
    int Px[4], Py[4];
    int Kx[4], Ky[4];

    for( i = 0; i < 4; i++ )
    {
        Px[i] = parse_num( );
        Py[i] = parse_num( );
    }
    ox = Px[0];
    oy = Py[0];
    stepper_move( ox, oy );
    bezier_prep( Kx, Px );
    bezier_prep( Ky, Py );
    for( i = 0; i <= 128; i++ )
    {
        float t = i / 128.0f;

        x = bezier_eval( Kx, t );
        y = bezier_eval( Ky, t );
        if( x != ox || y != oy )
            stepper_draw( x, y );
        ox = x;
        oy = y;
    }
}

static void cli_flash_write( char *args )
{
    uint16_t page;
    uint8_t buffer[264] = {0};
    int page_val;
    int byte_val;
    int count = 0;
    
    if( sscanf( args, "%d", &page_val ) != 1 || page_val < 0 || page_val > 2047 )
    {
        printf( "Usage: flashwrite <page 0-2047> <bytes...>\n" );
        return;
    }
    page = (uint16_t)page_val;

    while( *args && *args != ' ' ) args++;
    while( *args == ' ' ) args++;

    while( *args && count < 264 )
    {
        if( sscanf( args, "%i", &byte_val ) == 1 )
        {
            buffer[count++] = (uint8_t)byte_val;
        }
        while( *args && *args != ' ' ) args++;
        while( *args == ' ' ) args++;
    }

    printf( "Writing %d bytes to Flash Page %d...\n", count, page );
    flash_write_page( page, buffer );
    printf( "Done.\n" );
}

static void parse_flash( void )
{
    flash_test( );
}

static void parse_pressure( void )
{
    stepper_pressure( parse_num() );
}

static void parse_reset( void )
{
    cli( );
    wdt_enable( WDTO_15MS );
    while( 1 ); 
}

static void parse_hpgl( void )
{
    spooler_init();

    for( uint8_t i = curtok; i < argc; i++ )
    {
        char *str = argv[i];
        while( *str )
        {
            spooler_write_byte( (uint8_t)(*str++) );
        }
        spooler_write_byte( ' ' );
    }

    spooler_finish_write();
    spooler_execute_from_flash();

    curtok = argc;
}

static void parse( char *buf_ptr )
{
    (void)buf_ptr;
    curtok = 0;

    uint8_t tok = get_token();

    // Prevent high-frequency streaming commands from clearing the progress display
    if( tok != C_EOL && tok != C_PROGRESS && tok != C_DONE && 
        tok != C_MOVE && tok != C_DRAW && tok != C_SPEED && tok != C_PRESS )
    {
        lcd_clear();
        fprintf( &lcd, "CMD: %s", buf ); 
    }

    curtok = 0;
    get_token();

    switch( tok )
    {
        case C_VERSION:    version( ); break;
        case C_MOVE:       parse_move( ); break;
        case C_DRAW:       parse_draw( ); break;
        case C_SPEED:      parse_speed( ); break;
        case C_PRESS:      parse_pressure( ); break;
        case C_CURVE:      parse_curve( ); break;
        case C_FLASH:      parse_flash( ); break;
        case C_FLASHWRITE: 
            cli_flash_write( buf + (token.lex - buf) + strlen(token.lex) ); 
            curtok = argc;
            break;
        case C_RESET:      parse_reset( ); break;
        case C_LEDS:       parse_leds( ); break;
        case C_LED:        parse_single_led( ); break;
        case C_HPGL:       parse_hpgl( ); break;
        case C_PROGRESS:   parse_progress(); break;

        case C_DONE:
            lcd_clear();
            fprintf( &lcd, "Done!" );
            break;

        case C_EOL:        break;

        default:
            lcd_clear();
            fprintf( &lcd, "Unknown Cmd" );
            printf( "unknown command '%s'\n", token.lex );
            return;
    }
    
    if( get_token() != C_EOL )
        printf( "unrecognized parameter '%s'\n", token.lex );
}

void cli_poll( void )
{
    int c = usb_peek( );

    if( c < 0 )
        return;
    if( c == '\r' || c == '\n' )
    {
        printf( "\n" );
        buf[buf_len] = 0;
        buf_len = 0;
        argc = split_line( buf, argv );
        if( !setjmp(cmd_error) )
            parse( buf );
        printf( "%d>", stepper_queued() );
    }
    else if( c == '\b' || c == 0x7f )
    {
        if( buf_len > 0 )
        {
            buf_len--;
            if( echo )
                printf( "\b \b" );
        }
    }
    else if( c == 5 )
        echo ^= 1;
    else if( isprint(c) && buf_len < BUFLEN )
    {
        if( echo )
            putchar( c );
        buf[buf_len++] = c;
    }
}