/*
 * stepper.h
 *
 * Header interface for stepper X/Y/Z motion control.
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

#ifndef STEPPER_H
#define STEPPER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configures GPIO pin directions for motor drivers, pen solenoids, and home switch inputs.
 */
void stepper_init( void );

/**
 * @brief Main timer interrupt handler that executes stepper motor microsteps and manages state machines.
 */
void stepper_tick( void );

/**
 * @brief Enqueues a rapid traverse move to target coordinates (x, y) with pen lifted.
 * @param x Target X coordinate (mat direction).
 * @param y Target Y coordinate (carriage direction).
 */
void stepper_move( int x, int y );

/**
 * @brief Enqueues a cutting line operation to target coordinates (x, y) with pen lowered.
 * @param x Target X coordinate.
 * @param y Target Y coordinate.
 */
void stepper_draw( int x, int y );

/**
 * @brief Enqueues a command to adjust line step delay (motion speed).
 * @param speed Step delay parameter.
 */
void stepper_speed( int speed );

/**
 * @brief Enqueues a command to set cutting tool downward force (PWM duty cycle).
 * @param pressure Downward force setting.
 */
void stepper_pressure( int pressure );

/**
 * @brief Gets current asynchronous coordinates for debug tracking.
 * @param x Output pointer for current X coordinate.
 * @param y Output pointer for current Y coordinate.
 */
void stepper_get_pos( int *x, int *y );

/**
 * @brief Checks if a mat or cutting sheet is currently loaded.
 * @return 1 if loaded (x >= 0), 0 if unloaded (x < 0).
 */
uint8_t stepper_is_material_loaded( void );

/**
 * @brief Returns the number of pending commands remaining in the ring buffer.
 * @return Count of active queue items.
 */
int stepper_queued( void );

/**
 * @brief Moves mat out of the machine beyond coordinate origins.
 */
void stepper_unload_paper( void );

/**
 * @brief Moves mat into position under drive rollers.
 */
void stepper_load_paper( void );

#ifdef __cplusplus
}
#endif

#endif // STEPPER_H