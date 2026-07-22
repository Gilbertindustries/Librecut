#ifndef KEYPAD_H
#define KEYPAD_H

#include <stdint.h>

void keypad_init( void );
int  keypad_scan( void );
void keypad_update_load_led( void );
void keypad_set_leds( uint16_t mask );
void keypad_leds_enable( uint8_t enable );
char keypad_stop_pressed( void );

#endif // KEYPAD_H