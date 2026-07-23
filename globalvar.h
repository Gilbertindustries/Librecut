/*
 * globalvar.h
 *
 * Global state variables shared across Librecut firmware modules.
 */

#ifndef GLOBALVAR_H
#define GLOBALVAR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Shared Global Variables
 * ------------------------------------------------------------------------- */

// Example: Holds temporary message text or machine flags
extern volatile uint8_t g_jog_active;       // Flag set to 1 when user is actively jogging
extern int16_t          g_custom_origin_x; // Tracks user-defined local X origin offset
extern int16_t          g_custom_origin_y; // Tracks user-defined local Y origin offset

void globalvar_init( void );

#ifdef __cplusplus
}
#endif

#endif // GLOBALVAR_H