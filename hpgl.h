/*
 * hpgl.h
 *
 * HPGL protocol parser for Freecut firmware.
 */

#ifndef HPGL_H
#define HPGL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void hpgl_init( void );
void hpgl_process_char( char c );
void hpgl_process_string( const char *str );

#ifdef __cplusplus
}
#endif

#endif // HPGL_H