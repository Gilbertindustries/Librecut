/*
 * spooler.c
 *
 * External DataFlash Spooler for Freecut.
 * Buffers incoming stream to AT45DB041B DataFlash and plays it back to HPGL parser.
 */

#include <stdio.h>
#include <stdint.h>
#include "flash.h"
#include "hpgl.h"
#include "spooler.h"

#define FLASH_PAGE_SIZE 264
#define AT_CAR          0xe8  // Continuous Array Read Command

static uint8_t page_buf[FLASH_PAGE_SIZE];
static uint16_t page_buf_idx = 0;
static uint16_t current_page = 0;
static uint32_t total_bytes_spooled = 0;

void spooler_init( void )
{
    page_buf_idx = 0;
    current_page = 0;
    total_bytes_spooled = 0;
}

void spooler_write_byte( uint8_t byte )
{
    page_buf[page_buf_idx++] = byte;
    total_bytes_spooled++;

    if( page_buf_idx >= FLASH_PAGE_SIZE )
    {
        flash_write_page( current_page++, page_buf );
        page_buf_idx = 0;
    }
}

void spooler_finish_write( void )
{
    if( page_buf_idx > 0 )
    {
        // Zero-pad remainder of the final page
        for( uint16_t i = page_buf_idx; i < FLASH_PAGE_SIZE; i++ )
        {
            page_buf[i] = 0;
        }
        flash_write_page( current_page++, page_buf );
        page_buf_idx = 0;
    }
}

void spooler_execute_from_flash( void )
{
    if( total_bytes_spooled == 0 ) return;

    hpgl_init();

    // Initiate Continuous Array Read starting at page 0
    flash_write_cmd( AT_CAR, 0 );
    flash_clocks( 32 ); // 32 dummy clocks required by AT45DB041B

    for( uint32_t i = 0; i < total_bytes_spooled; i++ )
    {
        uint8_t c = flash_read_byte();
        if( c != 0 ) // Skip null-padding from final page
        {
            hpgl_process_char( (char)c );
        }
    }

    flash_cs_high(); // De-assert chip select when finished
}