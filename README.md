# Librecut
No longer fear, my fellow circut crv001 owners, as i am here. 

Firmware for the crv001 to make it useful in the modern day

__Cricut is a trademark of PROVO CRAFT.__

Freecut or Librecutter is not a PROVO CRAFT product. Freecut was developed completely 
independently and so was Librecutter , without any documentation to PROVO CRAFT products, and without
any original firmware. If you have problems with Freecut, do not consult
PROVO CRAFT. 

## Librecut aims to modernize and add functions to Arlet's Freecut

Librecutter

Librecutter is a open-source free firmware for the Cricut(TM) CRV001 Personal Electronic cutter. 

It allows computer control via half baked HPGL support and python utilities 

# Flashing guide

You will need a avr programmer. 

J5 is the header that is used to program the cutter on the mainboard. 

 J5   |  AVR
------+-----
  1   |  PDO
  3   |  SCK
  4   |  PDI
  5   |  RESET
  6   |  GND

Top view of connector:

  J5 Silkscreem
+----        ---+
| 9  7  5  3  1 |
|10  8  6  4  2 |
+---------------+

Wire pins 1, 3, 4, 5, 6 to the respective pins on your arduino or avr programmer

Cricut Header            Standard AVR ISP
  (1) PDO   ──────────────>  MOSI
  (2) +5V   ──────────────>  VCC
  (3) SCK   ──────────────>  SCK
  (4) PDI   ──────────────>  MISO
  (5) RESET ──────────────>  RESET
  (6) GND   ──────────────>  GND

Please compile with Arduino IDE i dont think the Makefile works

https://docs.arduino.cc/built-in-examples/arduino-isp/ArduinoISP/

