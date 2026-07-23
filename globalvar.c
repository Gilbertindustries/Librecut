/*
 * globalvar.c
 *
 * Storage definition for global state variables.
 */

#include "globalvar.h"

/* -------------------------------------------------------------------------
 * Variable Definitions (Memory Allocation)
 * ------------------------------------------------------------------------- */

volatile uint8_t g_jog_active       = 0;
int16_t          g_custom_origin_x = 0;
int16_t          g_custom_origin_y = 0;

/**
 * @brief Resets or initializes default global state values on startup.
 */
void globalvar_init( void )
{
    g_jog_active       = 0;
    g_custom_origin_x = 0;
    g_custom_origin_y = 0;
}