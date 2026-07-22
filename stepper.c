/*
 * stepper.c
 *
 * Unipolar Stepper Motor & Toolhead Actuator Driver.
 */

#include <avr/wdt.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <inttypes.h>
#include <stdio.h>

#include "lcd.h"
#include "stepper.h"
#include "keypad.h"
#include "timer.h"

#define MAT_EDGE 250       // Distance to roll mat when loading/unloading
#define MAX_Y    2400      // Upper Y movement limit (steps)
#define MAX_X    4800      // Upper X movement limit (steps)

#define DEBUG    (1 << 5)  
#define debug_on()  do { PORTB |=  DEBUG; } while(0)
#define debug_off() do { PORTB &= ~DEBUG; } while(0)

/* PORTA & PORTC Motor Winding Outputs */
#define H0  0x01
#define F0  0x02
#define H1  0x04
#define F1  0x08
#define H2  0x10
#define F2  0x20
#define H3  0x40
#define F3  0x80

#define HOME (1 << 1) // PD1: Limit switch input pin
#define PEN  (1 << 2) // PE2: Tool solenoid driver output

#define at_home() (!(PIND & HOME)) // Active-Low check for Limit Switch

static const uint8_t phase[16] = 
{
    F0,       F0|H1,    F0|F1,    H0|F1,
    F1,       F1|H2,    F1|F2,    H1|F2,
    F2,       F2|H3,    F2|F3,    H2|F3,
    F3,       F3|H0,    F3|F0,    H3|F0,
};

static int x = -MAT_EDGE; 
static int y = 2400;

static int pressure = 1023; 

int pen_time[4] = { 10, 12, 40 };
static int pen_seq = -1;

static struct bresenham
{
    int step;     
    int steps;    
    int delta;    
    int error;    
    int dx;       
    int dy;       
    char steep;   
} b;

static int delay = 0; 

/* State Machine initialized using enum from stepper.h */
static stepper_state_t state = READY;

#define CMD_QUEUE_SIZE 8 

enum type
{
    MOVE,     
    DRAW,     
    SPEED,    
    PRESSURE  
};

struct cmd
{
    enum type type;
    int x, y;
};

static struct cmd cmd_queue[CMD_QUEUE_SIZE];
static volatile uint8_t cmd_head = 0;
static volatile uint8_t cmd_tail = 0;

uint8_t stepper_is_material_loaded( void )
{
    return (x >= 0) ? 1 : 0;
}

int stepper_queued( void )
{
    return (int)((uint8_t)(cmd_head - cmd_tail));
}

void stepper_off( void )
{
    PORTA = 0;
    PORTC = 0;
}

static struct cmd *alloc_cmd( uint8_t type )
{
    struct cmd *cmd;

    while( (uint8_t)(cmd_head - cmd_tail) >= CMD_QUEUE_SIZE )
        wdt_reset( );
        
    cmd = &cmd_queue[cmd_head % CMD_QUEUE_SIZE];
    cmd->type = (enum type)type;
    return cmd;
}

static struct cmd *get_cmd( void )
{
    if( cmd_head == cmd_tail )
        return NULL;
    else
        return &cmd_queue[cmd_tail++ % CMD_QUEUE_SIZE];
}

static void pen_up( void )
{
    if( PORTE & PEN )
        delay = 50;
        
    PORTE &= ~PEN;
    pen_seq = -1;
}

static void pen_down( void )
{
    if( PORTE & PEN ) 
        return;

    timer_set_pen_pressure( 1023 );
    PORTE |= PEN; 
    pen_seq = 0;
    delay = pen_time[2];
}

void stepper_home( void )
{
    cmd_tail = cmd_head; // Clear buffer queue
    pen_up();
    
    lcd_clear();
    lcd_puts("Homing...");
    
    state = HOME1;
}

uint8_t stepper_get_state( void )
{
    return (uint8_t)state;
}

void stepper_move( int target_x, int target_y )
{
    if( target_x < -MAT_EDGE || target_x > MAX_X || target_y < 0 || target_y > MAX_Y )
        return;

    struct cmd *cmd = alloc_cmd( MOVE );
    cmd->x = target_x;
    cmd->y = target_y;
    cmd_head++;
}

void stepper_draw( int target_x, int target_y )
{
    if( target_x < 0 || target_x > MAX_X || target_y < 0 || target_y > MAX_Y )
        return;

    struct cmd *cmd = alloc_cmd( DRAW );
    cmd->x = target_x;
    cmd->y = target_y;
    cmd_head++;
}

void stepper_speed( int speed_val )
{
    struct cmd *cmd = alloc_cmd( SPEED );
    cmd->x = speed_val;
    cmd_head++;
}

void stepper_load_paper( void )
{
    if( x < 0 )
    {
        stepper_speed( 250 );   
        stepper_move( 0, y ); 
    }
    stepper_speed( 100 );
    stepper_move( 0, 0 );
}

void stepper_unload_paper( void )
{
    stepper_speed( 100 );
    stepper_move( -MAT_EDGE, 0 );
}

void stepper_pressure( int pressure_val )
{
    struct cmd *cmd = alloc_cmd( PRESSURE );
    cmd->x = pressure_val;
    cmd_head++;
}

void stepper_get_pos( int *px, int *py )
{
    *px = x;
    *py = y;
}

static void bresenham_init( int x1, int y1 )
{
    int dx_val, dy_val;

    if( x1 > x ) 
    { 
        b.dx = 1;
        dx_val = x1 - x;
    }
    else
    {
        b.dx = -1;
        dx_val = x - x1;
    }

    if( y1 > y )
    {
        b.dy = 1;
        dy_val = y1 - y;
    }
    else
    {
        b.dy = -1;
        dy_val = y - y1;
    }

    if( dx_val > dy_val )
    {
        b.steep = 0;
        b.steps = dx_val;
        b.delta = dy_val;
    }
    else
    {
        b.steep = 1;
        b.steps = dy_val;
        b.delta = dx_val;
    }

    b.step = 0;
    b.error = b.steps / 2;
}

static void bresenham_step( void )
{
    if( b.step >= b.steps )
    {
        state = READY;
        return;
    }
    
    b.step++;
    b.error -= b.delta;
    
    if( b.error < 0 )
    {
        b.error += b.steps;
        x += b.dx;
        y += b.dy;
    }
    else if( b.steep )
    {
        y += b.dy;
    }
    else
    {
        x += b.dx;
    }
}

static stepper_state_t do_next_command( void )
{
    struct cmd *cmd = get_cmd( );

    if( !cmd )
    { 
        pen_up( );
        return READY;
    }

    switch( cmd->type )
    {
        case MOVE: 
        case DRAW: 
            if( x == cmd->x && y == cmd->y )
                return READY;

            if( cmd->type == MOVE )
                pen_up(); 
            else
                pen_down( );

            bresenham_init( cmd->x, cmd->y );
            return LINE;

        case PRESSURE:
            pressure = cmd->x;
            timer_set_pen_pressure( pressure );
            break;

        case SPEED:
            timer_set_stepper_speed( cmd->x );
            break;
    }

    return READY;
}

void stepper_tick( void )
{
    static uint16_t led_blink_counter = 0;

    if( pen_seq >= 0 )
    {
        pen_seq++;
        if( pen_seq >= pen_time[1] )
        {
            PORTE |= PEN;
            timer_set_pen_pressure( pressure );
            pen_seq = -1;
        }
        else if( pen_seq >= pen_time[0] )
        {
            PORTE &= ~PEN;
        }
    }
    
    if( delay && --delay )
        return;

    if( keypad_stop_pressed() ) 
    {
        state = READY;
        x = 0;
        cmd_tail = cmd_head; 
        pen_up();
        stepper_off( );
        keypad_set_leds( 0x0000 ); 
    }

again:
    switch( state )
    {
        case HOME1:
            if( !at_home() && y > 0 )
                y--;
            else
                state = HOME2; 
            break;

        case HOME2:
            if( at_home() )
                y++;
            else
            {
                y = 0;
                state = READY;
                lcd_clear();
                lcd_puts("Loading...");
            }
            break;

        case READY:
            state = do_next_command( );
            if( state != READY )
                goto again;
            break;

        case LINE:
            bresenham_step( );
            break;
    }

    if( state == READY )
    {
        stepper_off( ); 
        keypad_set_leds( 0x0000 ); 
        led_blink_counter = 0;
    }
    else
    {
        PORTA = phase[x & 0x0F]; 
        PORTC = phase[y & 0x0F]; 

        led_blink_counter++;
        if( led_blink_counter >= 400 )
        {
            keypad_set_leds( 0x0000 ); 
            led_blink_counter = 0;
        }
    }
}

void stepper_init( void )
{
    stepper_off( );
    pen_up( );

    DDRA = 0xFF;  
    DDRC = 0xFF;  
    DDRE |= PEN;  
    
    PORTD |= HOME; 
}