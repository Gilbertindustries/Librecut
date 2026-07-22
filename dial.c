/*
 * dial.c
 *
 * Driver for 3 analog dials, connected as follows:
 *
 * dial  | AVR
 *-------+-----------
 * size  | PF0 (ADC0)
 * speed | PF1 (ADC1)
 * press | PF2 (ADC2)
 *
 * Each input is a simple voltage divider between 0 and 5V, with 
 * a few discrete settings where the pot clicks.
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
 *
 */

#include <avr/interrupt.h>
#include <avr/io.h>
#include <inttypes.h>
#include <stdio.h>
#include "dial.h"

static uint8_t channel = 0;

static unsigned char dial_adc[MAX_DIALS];
static unsigned char dial_steps[MAX_DIALS] = { 10, 5, 5 };

static int dial_setting( uint8_t dial )
{
    return( (dial_adc[dial] * (dial_steps[dial]-1) + 125) / 250 );
}

int dial_get_speed( void )
{
    return dial_setting( DIAL_SPEED );
}

int dial_get_pressure( void )
{
    return dial_setting( DIAL_PRESSURE );
}

int dial_get_size( void )
{
    return dial_setting( DIAL_SIZE );
}

/* --- Mapped Values for Hardware Controls --- */

static const uint16_t speed_delays[5] = {
    500, // Notch 1 (Index 0): Slowest step delay
    350, // Notch 2 (Index 1)
    200, // Notch 3 (Index 2)
    120, // Notch 4 (Index 3)
    60   // Notch 5 (Index 4): Fastest step delay
};

uint16_t dial_get_mapped_speed( void )
{
    int step = dial_get_speed(); // Returns 0..4
    if( step < 0 ) step = 0;
    if( step > 4 ) step = 4;
    
    return speed_delays[step];
}

static const uint16_t pressure_values[5] = {
    200,  // Notch 1 (Index 0): Lightest PWM power
    400,  // Notch 2 (Index 1)
    600,  // Notch 3 (Index 2)
    800,  // Notch 4 (Index 3)
    1023  // Notch 5 (Index 4): Full solenoid power
};

uint16_t dial_get_mapped_pressure( void )
{
    int step = dial_get_pressure(); // Returns 0..4
    if( step < 0 ) step = 0;
    if( step > 4 ) step = 4;
    
    return pressure_values[step];
}

/* --- ADC Polling & Control --- */

void dial_poll( void )
{
    // Clear interrupt flag and store 8-bit left-adjusted result
    ADCSRA |= (1 << ADIF);
    dial_adc[channel] = ADCH;
    
    if( ++channel == MAX_DIALS )
        channel = 0;
        
    ADMUX = (1 << ADLAR) | (1 << REFS0) | channel;
    ADCSRA |= (1 << ADSC);
}

void dial_init( void )
{
    ADMUX = (1 << ADLAR) | (1 << REFS0) | channel;
    ADCSRA = (1 << ADEN) | (1 << ADSC) | 7; // Enable ADC with prescaler = 128
}