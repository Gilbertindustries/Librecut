# Librecut

*No longer fear, my fellow owners of Cricut's from the Bush Administration, as I am here.*

Are you sick and tired of using libcutter with SCAL? Are you sick of Cartridges? Ready for something new and FREE?

**Librecut** (formerly *Librecutter*) is open-source custom firmware that builds on Arlet's Freecut for the **Cricut™ CRV001 Personal Electronic Cutter** (The first Cricut ever) to make it useful in the modern era.

It enables direct computer control of the machine using basic """""HPGL language support""""" and custom Python utilities. (Please use the Python Utilities)

There is also an Inkscape plugin inside of the utils directory, this is the recommended way to cut files
---
# DISCLAMER

Instaling Librecut WILL void your nonexsistant warrenty! 

You CANNOT revert to the original firmware. 

Librecut will render your cartridges USELESS with the device!  

Failed installation of Librecut may render your device USELESS! 

Librecut has ONLY been tested on the CRV001!

This is a software mod that you CANNOT REVERSE! 
---

 **Cricut** is a trademark of **Cricut Incorporated** Formerly Provo Craft.  
> Freecut and Librecut are not Cricut Inc products. These projects were developed completely independently without official documentation or original firmware code from Cricut Inc. If you encounter issues, do not contact Cricut Inc.

---

## License

This project is licensed under the **GNU General Public License v2.0** (GPLv2) - see the [LICENSE](LICENSE) file for details. 

Portions of this project are derived from Arlet's Freecut project, also licensed under GPLv2.

## Overview

Librecut aims to modernize and expand upon **Arlet’s Freecut** (thank you goat) firmware, providing:
Open-source control for legacy hardware. (CRV001)
USB-to-HPGL plotting and cutting execution.
Python-based CLI utilities for sending commands.

---

## Flashing Guide

To flash this firmware onto the Cricut mainboard, you will need an **AVR Programmer** (such as a USBasp, AVRISP, or an **Arduino** running the [ArduinoISP sketch](https://docs.arduino.cc/built-in-examples/arduino-isp/ArduinoISP/)).

You will also need the [Arduino IDE](https://www.arduino.cc/en/software/) to compile Librecut. 

### 1. Board Header Location & Layout

Locate header **`J5`** on the Cricut mainboard. Looking at the **top view** of the board, the pin arrangement relative to the silkscreen is structured as follows:

```text
       J5 Silkscreen
+-------------------------+
|  9   7   5   3   1   -  |  <-- Odd Pins (Top Row)
| 10   8   6   4   2   -  |  <-- Even Pins (Bottom Row)
+-------------------------+

- is not a pin please ignore

Cricut Header (J5)          Standard AVR / Arduino ISP
   (1) PDO   ------------->  MOSI (Pin 11 on Arduino Uno)
   (2) +5V   ------------->  VCC  (5V)
   (3) SCK   ------------->  SCK  (Pin 13 on Arduino Uno)
   (4) PDI   ------------->  MISO (Pin 12 on Arduino Uno)
   (5) RESET ------------->  RESET (Pin 10 on Arduino Uno as ISP)
   (6) GND   ------------->  GND
