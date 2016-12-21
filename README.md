CANbus sniffer using STM32F042 microcontroller
==============================================

This implementation was born out of a challenge to myself to create a CANbus sniffer less complicated than existing commercial products.

There are a bevy of existing CANbus products, but they seem to universally rely on two ICs working in tandem: one provides a USB-to-serial interface and the other (generally a microcontroller) implements the CAN sniffing.  This approach creates a bevy of implementation complications.  The USB-to-serial interface has one bit rate, but then the CAN microcontroller has another.  If the USB-to-serial interface runs at a rate lower than the CAN microcontroller, data is lost during heavy traffic.  Furthermore, the CAN microcontroller is oblivious to the USB-to-serial rate, so there must be a protocol invented on top of the serial data to configure *both* the CANbus rate of the CAN microcontroller and the USB-to-serial rate.  All this is inelegant and prone to user misconfiguration.

This sniffer uses a single IC (STM32F042 USB microcontroller) to output received messages on the CANbus in the "LAWICEL" protocol form over a USB virtual CDC serial port.

## Requirements

[Rowley Crossworks for ARM](http://www.rowley.co.uk/arm/) is needed to compile this code.  The source code is gcc-friendly, but you must adapt the code yourself if you wish to adopt a different tool chain.

## Hardware Tradeoffs

The designers of the STMicro STM32F0xx family of parts made the unfortunate decision to assign USB and CAN peripheral functionality to the same pins.

There is an additional pin assignment option, but it is for CAN_RX only.  As a result, this implementation is forced to be receive-only (e.g. a sniffer).

The CAN PHY transceiver is powered directly from the +5V provided by the USB port.  The microcontroller itself is powered from a +3.3V rail; this mix of power rails is possible because the microcontroller is +5V tolerant on the CAN_RX pin.

A further complication on pin assignments is that the only available CAN_RX pin shares its functionality with the BOOT0 signal used by the mask ROM bootloader.  As a result, the firmware must be programmed with its "Option bytes" configured to defeat the BOOT0 functionality.  Consult Chapter 4 of RM0091 for details on these bytes, and configure your programming utility to set these bytes.

To provide electrical isolation between the CANbus and the PC, consider generating a +5V supply for the CAN PHY transceiver from vehicle power, and use a digital isolator IC between the CAN PHY transceiver and USB microcontroller.

