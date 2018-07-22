# Chromaduino
A driver for a (slave) Funduino/Colorduino 8x8 RGB LED board.  The LED display can then be controlled by an I2C master.
This leaves the master Arduino with pins available for other functions, like reading buttons etc.  
It's also easier to reprogram a master.

A driver for a Funduino/Colorduino (slave) board: 
* an 8x8 RGB LED matrix common anode
* an ATMEGA328P connected to
  * a DM163 which drives 24 channels of PWM data connected to 8x Red, Green and Blue LED columns.  
      ATMEGA328P pins PD6 & 7 are SCL & SDA to DM163 pins DCK & SIN. DM163 pins IOUT0-23 are connected to LED RGB row pins
  * an M54564FP which drives the LED rows.
      ATMEGA328P pins PB0-4 & PD3,4 connect to M54564FP pins IN1-8. M54564FP pins OUT1-8 connect to LED pins VCC0-7
  * an EXTERNAL master Arduino device driving the LED colours via I2C/Wire (address 0x70)
  
This code is based on the "Colorduino" library/demo (https://www.itead.cc/blog/colorduino-schematic-and-demo-code) but simplified/clarified and with the addition of a simple I2C master/slave protocol.

Commands are a 1-byte transmission from the Master to the Chromaduino (Slave, address 0x70).  Data is a 3-byte transmission. 
The Chromaduino has two RGB channel buffers of 3x8x8 bytes.  One buffer (READ) is being read from to drive the LED display. 
The other buffer (WRITE) is being written to by the Master.\
There is a third buffer, FAST, which is 12 bytes long (see command 0x11)


Commands:

0x00 sets the write pointer to the start of the WRITE buffer.

0x01 swaps the WRITE and READ buffers

0x02 takes the low 6 bits of the first 3 bytes in the WRITE buffer as GLOBAL scalars of the R, G and B values ("colour balance"). The default is 25, 63, 63.  This downplays the intensity of Red to achieve a reasonable "White".
If the first byte is 0x80 the command is ignored
      
0x03 takes the 1st byte in the WRITE buffer as the TCNT2 value, provided the 2nd and 3rd bytes are 16 (clock freq in MHz) and 128 (clock divisor). TCNT2 drives the timer which updates the LED rows. The default is 99 for a frequency of 800Hz.  
      For a desired FREQ in Hz, TCNT2 = 255 - CLOCK/FREQ where CLOCK=16000000/128. 
      
0x10 clears the WRITE buffer to all 0's

0x11 sets the write pointer to the start of FAST

Data:
  Triplets of bytes are written sequentially to the WRITE buffer as R, G & B (bytes beyond the buffer are ignored)
  EXCEPT that after an 0x11 command, triplets of bytes are written sequentially to the FAST buffer. 
  When the 4th triplet is received the buffer is interpreted as:\
     R,G,B,  flags,row0,row1,  row2,row3,row4,  row5,row6,row7\
     And the WRITE buffer is updated with the RGB value where a bit is set in the row data.  \
     If bit0 of flags is set, the WRITE and READ buffers are then swapped at the end.\
     If bit1 of flags is set, BLACK is written to the WRITE buffer where a bit is unset in the row data 
     (otherwise it is untouched)
  Other transmissions are ignored
  
Request:
  Requesting a single byte returns the number of bytes written to the WRITE buffer
  
Demo:
  If #define'd, a demo will run after 5s if no wire data is received from the Master
  
Example:\
  See ChromaduinoMaster.ino.  This just flips RGB's around.\
  See ChromaduinoScrollDemoMaster.ino.  This demonstrates scrolling text on two daisy-chained LED matrices (each must have a unique I2C address)

Programming:\
Program the board by (for example) popping the ATmega chip off a Duemilanove and connecting\
GND-GND, 5V-VCC, RX(D0)-RXD, TX(D1)-TXD and RESET-DTR\
and programming the Duemilanove as normal.  This will program the ATmega on the Funduino/Colorduino board.

v6 Mark Wilson 2016: original\
v7 Mark Wilson 2018: changed I2C address (was 0x05, non-standard?); added 0x10 & 0x11 commands; ignore white balance if R=0x80

