/*
 * timer.h
 *
 * Header interface for hardware timer management (Timer0, Timer1, Timer2, Timer3).
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

#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Global timing event flags set by Timer 0 CTC interrupt handler
extern volatile uint8_t flag_Hz;   // Set to 1 every 1 second (1 Hz)
extern volatile uint8_t flag_25Hz; // Set to 1 every 40 ms (25 Hz)

/**
 * @brief Configures Hardware Timers 0, 1, 2, and 3 with appropriate prescalers and PWM modes.
 */
void timer_init( void );

/**
 * @brief Microsecond delay loop based on Timer 0 counter tick resolution.
 * @param usecs Number of microseconds to delay (Max ~2000 us).
 */
void usleep( int usecs );

/**
 * @brief Millisecond delay loop utilizing repeated call loops.
 * @param msecs Number of milliseconds to delay.
 */
void msleep( unsigned msecs );

/**
 * @brief Enables piezo speaker frequency generation via Timer 3.
 * @param Hz Target output frequency in Hertz.
 */
void beeper_on( int Hz );

/**
 * @brief Disables piezo speaker sound generation by clearing driver output pin.
 */
void beeper_off( void );

/**
 * @brief Sets Timer 2 compare match period to adjust step interrupt frequency.
 * @param delay Step delay divisor value.
 */
void timer_set_stepper_speed( int delay );

/**
 * @brief Sets Timer 1 10-bit PWM output duty cycle to adjust toolhead cutting pressure.
 * @param pressure Pressure setting (0 = full pressure, 1023 = minimal pressure).
 */
void timer_set_pen_pressure( int pressure );

#ifdef __cplusplus
}
#endif

#endif // TIMER_H