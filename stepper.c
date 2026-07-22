/*
 * stepper.c
 *
 * Unipolar Stepper Motor & Toolhead Actuator Driver.
 *
 * Hardware Overview:
 * ------------------
 * - Motors: 6-wire unipolar steppers.
 * - Coils: 4 windings per motor. Each coil can be driven with:
 * 1. Full Current (Direct transistor connection)
 * 2. Half Current (Transistor + 47-Ohm inline resistor)
 * - Port Mapping:
 * - PORTA: Controls X-Axis Motor (Mat Roller / Feed)
 * - PORTC: Controls Y-Axis Motor (Carriage / Pen Arm)
 * - Pen / Z-Axis Controls:
 * - PE2: Digital IO pin toggles Pen Solenoid UP/DOWN.
 * - PB6: Hardware PWM output adjusts down-force cutting pressure.
 * - Home Switch:
 * - PD1: Active-Low limit switch detecting home alignment.
 *
 * Resolution & Operating Space:
 * -----------------------------
 * Resolution: ~400 steps per inch.
 * Travel Volume: 6" x 12" Mat space (~2400 x 4800 discrete microsteps).
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
#include <avr/io.h>
#include <avr/interrupt.h>
#include <inttypes.h>
#include <stdio.h>

#include "lcd.h"
#include "stepper.h"
#include "keypad.h"
#include "timer.h"

/* -------------------------------------------------------------------------
 * Hardware Boundaries & Pin Mask Definitions
 * ------------------------------------------------------------------------- */
#define MAT_EDGE 250       // Distance to roll mat when loading/unloading
#define MAX_Y    2400      // Upper Y movement limit (steps)
#define MAX_X    4800      // Upper X movement limit (steps)

#define DEBUG    (1 << 5)  // PB5 used for timing debug diagnostics
#define debug_on()  do { PORTB |=  DEBUG; } while(0)
#define debug_off() do { PORTB &= ~DEBUG; } while(0)

/* PORTA & PORTC Motor Winding Outputs */
#define H0  0x01  // Coil 0 - Half Current
#define F0  0x02  // Coil 0 - Full Current
#define H1  0x04  // Coil 1 - Half Current
#define F1  0x08  // Coil 1 - Full Current
#define H2  0x10  // Coil 2 - Half Current
#define F2  0x20  // Coil 2 - Full Current
#define H3  0x40  // Coil 3 - Half Current
#define F3  0x80  // Coil 3 - Full Current

#define HOME (1 << 1) // PD1: Limit switch input pin
#define PEN  (1 << 2) // PE2: Tool solenoid driver output

#define at_home() (!(PIND & HOME)) // Active-Low check for Limit Switch

/* -------------------------------------------------------------------------
 * 16-Phase Microstepping Table
 * -------------------------------------------------------------------------
 * Interpolates full and half current combinations across four motor coils
 * to achieve 16 sub-step configurations per electrical cycle.
 */
static const uint8_t phase[16] = 
{
    F0,       F0|H1,    F0|F1,    H0|F1,
    F1,       F1|H2,    F1|F2,    H1|F2,
    F2,       F2|H3,    F2|F3,    H2|F3,
    F3,       F3|H0,    F3|F0,    H3|F0,
};

/* Current position variables */
static int x = -MAT_EDGE; 
static int y = 2400;

static int pressure = 1023; // Tool pressure parameter (PWM value)

/* Soft pen drop timing state variables */
int pen_time[4] = { 10, 12, 40 };
static int pen_seq = -1;

/* -------------------------------------------------------------------------
 * Bresenham Line Calculation State
 * ------------------------------------------------------------------------- */
static struct bresenham
{
    int step;     // Current step index
    int steps;    // Total steps along major axis
    int delta;    // Total steps along minor axis
    int error;    // Accumulated Bresenham decision error
    int dx;       // X-direction increment (+1 or -1)
    int dy;       // Y-direction increment (+1 or -1)
    char steep;   // Flag: 1 if Y is major axis, 0 if X is major axis
} b;

static int delay = 0; // Tick delay counter between discrete motor steps

/* Motion Engine State Machine */
static enum state
{
    HOME1,  // Seeking home switch
    HOME2,  // Backing off home switch
    READY,  // Idle mode, standby for queue commands
    LINE    // Interpolating active line segment
} state = READY;

/* -------------------------------------------------------------------------
 * Command Ring Buffer Queue
 * ------------------------------------------------------------------------- */
#define CMD_QUEUE_SIZE 8 // Buffer size (Must remain a power of two)

enum type
{
    MOVE,     // Pen-Up Rapid Traverse
    DRAW,     // Pen-Down Cutting Motion
    SPEED,    // Set Speed Parameter
    PRESSURE  // Set Pressure Parameter
};

struct cmd
{
    enum type type;
    int x, y;
};

static struct cmd cmd_queue[CMD_QUEUE_SIZE];
static volatile uint8_t cmd_head = 0;
static volatile uint8_t cmd_tail = 0;

/**
 * @brief Checks if a mat or cutting sheet is currently loaded.
 * @return 1 if loaded, 0 if unloaded (-MAT_EDGE position).
 */
uint8_t stepper_is_material_loaded( void )
{
    return (x >= 0) ? 1 : 0;
}

/**
 * @brief Computes remaining unexecuted commands in queue.
 */
int stepper_queued( void )
{
    return (int)((uint8_t)(cmd_head - cmd_tail));
}

/**
 * @brief Cuts power to motor coil transistors.
 */
void stepper_off( void )
{
    PORTA = 0;
    PORTC = 0;
}

/**
 * @brief Allocates space in command queue (Blocks/Resets Watchdog if full).
 */
static struct cmd *alloc_cmd( uint8_t type )
{
    struct cmd *cmd;

    while( (uint8_t)(cmd_head - cmd_tail) >= CMD_QUEUE_SIZE )
        wdt_reset( );
        
    cmd = &cmd_queue[cmd_head % CMD_QUEUE_SIZE];
    cmd->type = (enum type)type;
    return cmd;
}

/**
 * @brief Retrieves the next pending command from ring buffer (ISR Context).
 */
static struct cmd *get_cmd( void )
{
    if( cmd_head == cmd_tail )
        return NULL;
    else
        return &cmd_queue[cmd_tail++ % CMD_QUEUE_SIZE];
}

/* -------------------------------------------------------------------------
 * Public Motion Control Methods
 * ------------------------------------------------------------------------- */

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
        stepper_move( 0, y ); // Pull mat under rollers first
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

/* -------------------------------------------------------------------------
 * Low-Level Actuator Helper Functions
 * ------------------------------------------------------------------------- */

static void pen_up( void )
{
    if( PORTE & PEN )
        delay = 50;
        
    PORTE &= ~PEN;
    pen_seq = -1;
}

static void pen_down( void )
{
    if( PORTE & PEN ) // Already down
        return;

    // Apply minimum pressure initially for soft landing
    timer_set_pen_pressure( 1023 );
    PORTE |= PEN; 
    pen_seq = 0;
    delay = pen_time[2];
}

/**
 * @brief Pre-calculates Bresenham parameters for linear vector interpolation.
 */
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

/**
 * @brief Advances Bresenham line algorithm by a single microstep.
 */
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

/**
 * @brief Processes next pending queue command.
 */
static enum state do_next_command( void )
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

/* -------------------------------------------------------------------------
 * Timer Interrupt Handler (Stepper Motion Tick Engine)
 * ------------------------------------------------------------------------- */
void stepper_tick( void )
{
    /* Static counter for blinking the keypad LEDs */
    static uint16_t led_blink_counter = 0;

    /* Soft Pen Landing State Handler */
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
    
    // Process step delay timing
    if( delay && --delay )
        return;

    // Abort active motion sequences immediately if hardware STOP key pressed
    if( keypad_stop_pressed() ) 
    {
        state = READY;
        x = 0;
        cmd_tail = cmd_head; // Flush command queue
        pen_up();
        stepper_off( );
        keypad_set_leds( 0x0000 ); // Turn off keypad lights on stop
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

    // Output microstepping phase signals to motor port registers
    if( state == READY )
    {
        stepper_off( ); // Turn off coil power while idle to prevent overheating
        keypad_set_leds( 0x0000 ); // Turn off keypad LEDs when idle
        led_blink_counter = 0;
    }
    else
    {
        PORTA = phase[x & 0x0F]; // Apply 4-bit phase lookup to X motor
        PORTC = phase[y & 0x0F]; // Apply 4-bit phase lookup to Y motor

        /* -----------------------------------------------------------------
         * Keypad LED Flashing Logic
         * ----------------------------------------------------------------- */
        // Toggle the keypad LEDs every 200 motion steps
        led_blink_counter++;
        if( led_blink_counter == 200 )
        {
         //   keypad_set_leds( 0xFFFF ); // Turn ALL keypad LEDs ON
        }
        else if( led_blink_counter >= 400 )
        {
            keypad_set_leds( 0x0000 ); // Turn ALL keypad LEDs OFF
            led_blink_counter = 0;
        }
    }
}

/**
 * @brief Configures port directions and default register states for motion subsystem.
 */
void stepper_init( void )
{
    stepper_off( );
    pen_up( );

    DDRA = 0xFF;  // Configure PORTA (X Motor Driver) as outputs
    DDRC = 0xFF;  // Configure PORTC (Y Motor Driver) as outputs
    DDRE |= PEN;  // Configure PE2 (Tool Actuator Solenoid) as output
    
    PORTD |= HOME; // Enable internal pull-up resistor on Limit Switch (PD1)
}