/*
 * dial.h
 *
 * Interface for front panel dials.
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

#ifndef DIALS_H
#define DIALS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{ 
    DIAL_SIZE,
    DIAL_SPEED,
    DIAL_PRESSURE,
    MAX_DIALS
};

/* --- Raw Notch Positions --- */
int dial_get_speed( void );
int dial_get_pressure( void );
int dial_get_size( void );

/* --- Mapped Hardware Values --- */
uint16_t dial_get_mapped_speed( void );
uint16_t dial_get_mapped_pressure( void );

/* --- Control & Initialization --- */
void dial_poll( void );
void dial_init( void );

#ifdef __cplusplus
}
#endif

#endif // DIALS_H