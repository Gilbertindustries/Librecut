#ifndef LCD_H
#define LCD_H

#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Export the global FILE stream for stdout redirection / fprintf
extern FILE lcd;

void lcd_init( void );
void lcd_clear( void );
void lcd_pos( uint8_t pos );
void lcd_puts( const char *str );
void lcd_update_status( void );
void lcd_backlight_on( void );
void lcd_backlight_off( void );

#ifdef __cplusplus
}
#endif

#endif // LCD_H