/*
 * flash.h
 *
 * Driver for serial dataflash AT45DB041B on main board.
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

#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes SPI pins and reads flash status byte.
 */
void flash_init( void );

/**
 * @brief Reads the AT45DB041B status register.
 * @return Status byte (Bit 7: 1 = Ready, 0 = Busy)
 */
uint8_t flash_read_status( void );

/**
 * @brief Simple test function to dump flash contents over USB serial.
 */
void flash_test( void );

/**
 * @brief Writes a 264-byte page to the AT45DB041B DataFlash chip.
 * @param page Page address (0 to 2047)
 * @param buffer Pointer to a 264-byte data buffer
 */
void flash_write_page( uint16_t page, uint8_t *buffer );

/* * --- Low-Level SPI Helper Interfaces for Spooler ---
 */

/**
 * @brief Drives Chip Select (!CS) low to activate flash chip.
 */
void flash_cs_low( void );

/**
 * @brief Drives Chip Select (!CS) high to deactivate flash chip.
 */
void flash_cs_high( void );

/**
 * @brief Writes a single byte over software bit-banged SPI.
 */
void flash_write_byte( uint8_t data );

/**
 * @brief Reads a single byte over software bit-banged SPI.
 */
uint8_t flash_read_byte( void );

/**
 * @brief Generates dummy clock cycles on the SCK line.
 */
void flash_clocks( uint8_t count );

/**
 * @brief Writes an 8-bit command and a 24-bit address to the flash chip.
 */
void flash_write_cmd( uint8_t cmd, uint32_t addr );

#ifdef __cplusplus
}
#endif

#endif // FLASH_H