/*
 * spooler.h
 *
 * DataFlash Spooler Interface for Freecut
 */

#ifndef SPOOLER_H
#define SPOOLER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void spooler_init( void );
void spooler_write_byte( uint8_t byte );
void spooler_finish_write( void );
void spooler_execute_from_flash( void );

#ifdef __cplusplus
}
#endif

#endif // SPOOLER_H