/*
 * cli.h
 *
 * Header interface for the Freecut serial Command Line Interface (CLI).
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

#ifndef CLI_H
#define CLI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Polls the USB UART input buffer for incoming characters, parses commands on newline.
 */
void cli_poll( void );

/**
 * @brief Prints firmware version over USB serial.
 */
void version( void );

/**
 * @brief Directly enables or disables keypad backlights.
 * @param enable 1 to turn on backlights, 0 to turn off.
 */
void keypad_leds_set( uint8_t enable );

#ifdef __cplusplus
}
#endif

#endif // CLI_H