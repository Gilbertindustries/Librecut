/*
 * timer.c
 *
 * AVR Hardware Timer Allocation & Subsystem Driver:
 * --------------------------------------------------
 * - Timer 0 (8-bit): System Event Heartbeat & Microsecond Delay Timer.
 * Runs in CTC mode at 250 Hz.
 * - Timer 1 (16-bit): Fast PWM Mode 7 (10-bit) on OC1B (PB6).
 * Controls solenoid downward pressure for cutting blade.
 * - Timer 2 (8-bit): Stepper Motor Step Tick Engine.
 * Runs in CTC mode generating steps via ISR(TIMER2_COMP_vect).
 * - Timer 3 (16-bit): Audio Tone Generator on OC3A (PE3).
 * Generates variable frequency output for piezobeeper.
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

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/wdt.h>
#include <string.h>
#include <stdio.h>

#include "timer.h"
#include "stepper.h"

/* Software division counters for Timer 0 (250 Hz base rate) */
static uint8_t count_Hz = 250;
static uint8_t count_25Hz = 10;

/* Volatile flag declarations for task scheduling */
volatile uint8_t flag_Hz = 0;
volatile uint8_t flag_25Hz = 0;

/* -------------------------------------------------------------------------
 * Interrupt Service Routines
 * ------------------------------------------------------------------------- */

/**
 * @brief Timer 0 Compare Match ISR (Fires @ 250 Hz).
 * Divides base 250 Hz tick rate to yield 25 Hz and 1 Hz periodic system flags.
 */
ISR( TIMER0_COMP_vect ) 
{
    // Divide 250 Hz by 10 -> 25 Hz event flag
    if( --count_25Hz == 0 )
    {
        count_25Hz = 10;
        flag_25Hz = 1;
    }
    
    // Divide 250 Hz by 250 -> 1 Hz event flag
    if( --count_Hz == 0 )
    {
        count_Hz = 250;
        flag_Hz = 1;
    }
}        

/**
 * @brief Timer 2 Compare Match ISR.
 * Triggers step evaluation logic for X and Y stepper motors.
 */
ISR( TIMER2_COMP_vect ) 
{
    stepper_tick( );
}        

/* -------------------------------------------------------------------------
 * Beeper Control Functions (Timer 3)
 * ------------------------------------------------------------------------- */

/**
 * @brief Configures Timer 3 output frequency and enables PE3 speaker pin.
 * @param Hz Target tone frequency in Hz.
 */
void beeper_on( int Hz )
{
    if( Hz <= 0 ) return;

    DDRE |= (1 << 3); // Set PE3 (OC3A) as output pin
    
    // Period calculation: F_CPU (16MHz/2 or 8MHz base clock) / Hz
    OCR3A = (uint16_t)((8000000UL + (Hz / 2)) / Hz - 1);
}

/**
 * @brief Disables speaker output pin.
 */
void beeper_off( void )
{
    DDRE &= ~(1 << 3); // Revert PE3 to input to silence tone
}

/* -------------------------------------------------------------------------
 * Blocking Delay Implementations (Timer 0 TCNT Polling)
 * ------------------------------------------------------------------------- */

/**
 * @brief Blocks execution for specified microseconds by checking Timer 0 count increments.
 * @param usecs Delay time in microseconds (approx 16 µs resolution per clock tick).
 */
void usleep( int usecs )
{
    if (usecs <= 0) return;

    // Timer 0 runs at 62.5 kHz (16 microseconds per tick with 1:256 prescaler at 16MHz)
    uint8_t ticks = (uint8_t)(usecs / 16);
    if( ticks == 0 ) ticks = 1;

    uint8_t start = TCNT0;
    while( (uint8_t)(TCNT0 - start) < ticks )
    {
        // Polling loop
    }
}

/**
 * @brief Blocks execution for specified milliseconds.
 * @param msecs Delay time in milliseconds.
 */
void msleep( unsigned msecs )
{
    while( msecs-- != 0 )
        usleep( 1000 );
}

/* -------------------------------------------------------------------------
 * Stepper Speed & Pen Pressure Control Registers
 * ------------------------------------------------------------------------- */

/**
 * @brief Adjusts Timer 2 compare register and prescaler to change step tick rate.
 * @param delay Desired step tick interval delay.
 */
void timer_set_stepper_speed( int delay )
{
    uint8_t prescaler = 4; // Prescaler CS22:CS20 = 100 (Divide by 64)

    // Clamp input limits
    if( delay < 30 )
        delay = 30; 
    else if( delay > 1000 )
        delay = 1000;

    // Halt Timer 2 clock while reconfiguring
    TCCR2 &= ~0x07;  

    // Dynamic prescaler switching for slow speeds
    if( delay > 256 )
    {
        delay /= 4;
        prescaler = 5; // Prescaler CS22:CS20 = 101 (Divide by 128/256)
    }

    OCR2 = (uint8_t)(delay - 1); // Set output compare limit
    TCCR2 |= prescaler;         // Restart Timer 2 with new prescaler
}

/**
 * @brief Sets Timer 1 PWM duty cycle on OC1B to adjust tool pressure solenoid.
 * @param pressure 10-bit value (0 = Maximum Force, 1023 = Minimum Force).
 */
void timer_set_pen_pressure( int pressure )
{
    if( pressure > 1023 )
        pressure = 1023;
        
    OCR1B = (uint16_t)pressure;  
}

/* -------------------------------------------------------------------------
 * Timer Hardware Initialization Routine
 * ------------------------------------------------------------------------- */

/**
 * @brief Configures control registers for Timers 0, 1, 2, and 3.
 */
void timer_init( void )
{
    /* ---------------------------------------------------------------------
     * Timer 0 Configuration: CTC Mode, 250 Hz System Tick Interrupt
     * --------------------------------------------------------------------- */
    TCCR0 = (1 << WGM01) | 6; // CTC Mode, Prescaler 1:256 -> 62.5 kHz clock tick
    OCR0  = 249;              // Compare match value: 62.5 kHz / 250 = 250 Hz interrupt
    TIMSK = (1 << OCIE0);     // Enable Timer 0 Output Compare Match Interrupt

    /* ---------------------------------------------------------------------
     * Timer 1 Configuration: 10-bit Fast PWM for Solenoid Control on PB6
     * --------------------------------------------------------------------- */
    DDRB   |= (1 << 6);       // Configure PB6 (OC1B) pin as Output
    TCCR1A = (1 << WGM11) | (1 << WGM10) | (1 << COM1B1); // Fast PWM 10-bit, non-inverting
    TCCR1B = (1 << WGM12) | 1; // Prescaler = 1 (No prescaling)
    OCR1B  = 1023;            // Default duty cycle (lowest tool pressure)

    /* ---------------------------------------------------------------------
     * Timer 2 Configuration: CTC Mode for Stepper Motor Pulse Timing
     * --------------------------------------------------------------------- */
    TCCR2  = (1 << WGM21) | 4; // CTC Mode, Prescaler 1:64 -> 250 kHz tick rate
    OCR2   = 99;               // Default compare period (2.5 kHz step rate)
    TIMSK |= (1 << OCIE2);     // Enable Timer 2 Output Compare Match Interrupt

    /* ---------------------------------------------------------------------
     * Timer 3 Configuration: Fast PWM / Toggle Mode for Audio Tone Generator
     * --------------------------------------------------------------------- */
    TCCR3A = (1 << COM3A0) | (1 << WGM31) | (1 << WGM30); // Toggle OC3A on compare match
    TCCR3B = (1 << WGM33) | (1 << WGM32) | 1;            // Fast PWM mode, clock undivided
}