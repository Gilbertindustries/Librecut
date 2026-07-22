/*
 * stepper.h
 *
 * Header interface for stepper X/Y/Z motion control.
 */

#ifndef STEPPER_H
#define STEPPER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Export State Enum so Librecut.ino can read state definitions */
typedef enum {
    HOME1,  // Seeking home switch
    HOME2,  // Backing off home switch
    READY,  // Idle mode, standby for queue commands
    LINE    // Interpolating active line segment
} stepper_state_t;

/**
 * @brief Configures GPIO pin directions for motor drivers, pen solenoids, and home switch inputs.
 */
void stepper_init( void );

/**
 * @brief Triggers the homing sequence, resets motion, and updates LCD status.
 */
void stepper_home( void );

/**
 * @brief Gets current internal state machine status.
 */
uint8_t stepper_get_state( void );

/**
 * @brief Main timer interrupt handler that executes stepper motor microsteps and manages state machines.
 */
void stepper_tick( void );

/**
 * @brief Enqueues a rapid traverse move to target coordinates (x, y) with pen lifted.
 */
void stepper_move( int x, int y );

/**
 * @brief Enqueues a cutting line operation to target coordinates (x, y) with pen lowered.
 */
void stepper_draw( int x, int y );

/**
 * @brief Enqueues a command to adjust line step delay (motion speed).
 */
void stepper_speed( int speed );

/**
 * @brief Enqueues a command to set cutting tool downward force (PWM duty cycle).
 */
void stepper_pressure( int pressure );

/**
 * @brief Gets current asynchronous coordinates for debug tracking.
 */
void stepper_get_pos( int *x, int *y );

/**
 * @brief Checks if a mat or cutting sheet is currently loaded.
 */
uint8_t stepper_is_material_loaded( void );

/**
 * @brief Returns the number of pending commands remaining in the ring buffer.
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