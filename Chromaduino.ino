#include <Wire.h>

// Chromaduino
// A driver for a Funduino/Colorduino (slave) board: 
// * an 8x8 RGB LED matrix common anode
// * an ATMEGA328P connected to
//   * a DM163 which drives 24 channels of PWM data connected to 8x Red, Green and Blue LED columns.  
//       ATMEGA328P pins PD6 & 7 are SCL & SDA to DM163 pins DCK & SIN. DM163 pins IOUT0-23 are connected to LED RGB row pins
//   * an M54564FP which drives the LED rows.
//       ATMEGA328P pins PB0-4 & PD3,4 connect to M54564FP pins IN1-8. M54564FP pins OUT1-8 connect to LED pins VCC0-7
//   * an EXTERNAL master Arduino device driving the LED colours via I2C/Wire (address 0x05)
// This code is based on the "Colorduino" library but simplified/clarified and with the addition of a simple I2C master/slave protocol.
//
// Commands are a 1-byte transmission from the Master to the Chromaduino (Slave).  Data is a 3-byte transmission. 
// The Chromaduino has two RGB channel buffers of 3x8x8 bytes.  One buffer (READ) is being read from to drive the LED display. 
// The other buffer (WRITE) is being written to by the Master.
//
// Commands:
// 0x00 sets the write pointer to the start of the WRITE buffer.
// 0x01 swaps the WRITE and READ buffers
// 0x02 takes the low 6 bits of the first 3 bytes in the WRITE buffer as GLOBAL scalars of the R, G and B values ("colour balance").  
//       The default is 25, 63, 63.  This downplays the intensity of Red to achieve a reasonable "White"
// 0x03 takes the 1st byte in the WRITE buffer as the TCNT2 value, provided the 2nd and 3rd bytes are 16 (clock freq in MHz) and 128 (clock divisor)
//       TCNT2 drives the timer which updates the LED rows. The default is 99 for a frequency of 800Hz.  
//       For a desired FREQ in Hz, TCNT2 = 255 - CLOCK/FREQ where CLOCK=16000000/128. 
// Data:
//   Triplets of bytes are written sequentially to the WRITE buffer as R, G & B (bytes beyond the buffer are ignored)
//   Other transmissions are ignored
// Request:
//   Requesting a single byte returns the number of bytes written to the WRITE buffer
// Demo:
//   If #define'd, a demo will run after 5s if no wire data is received from the Master
//
// Programming:
// Program the board by (for example) popping the ATmega chip off a Duemilanove and connecting 
//    GND-GND, 5V-VCC, RX(D0)-RXD, TX(D1)-TXD and RESET-DTR
// and programming the Duemilanove as normal.  This will program the ATmega on the Funduino/Colorduino board.

// v5 Mark Wilson 2016

#define DEMO  // enable demo

#define MATRIX_ROWS 8
#define MATRIX_COLS 8
#define MATRIX_CHANNELS 3
#define MATRIX_LEDS MATRIX_ROWS*MATRIX_COLS
#define MATRIX_ROW_CHANNELS MATRIX_COLS*MATRIX_CHANNELS

#define WIRE_DEVICE_ADDRESS 0x05

#define ROW_PORT1 PORTB
#define ROW_PORT2 PORTD
#define LED_PORT  PORTC
#define I2C_PORT  PORTD

#define LED_SELECT 0x01
#define LED_LATCH  0x02
#define LED_RESET  0x04

#define I2C_SCL 0x40
#define I2C_SDA 0x80

#define SET(  _reg, _bits) (_reg) |=  (_bits)
#define CLEAR(_reg, _bits) (_reg) &= ~(_bits)


// Two copies of the LED data, one being read from to drive the LEDs in an ISR, another being written to, via I2C
byte matrixDataA[MATRIX_ROWS*MATRIX_ROW_CHANNELS];
byte matrixDataB[MATRIX_ROWS*MATRIX_ROW_CHANNELS];
byte defaultCorrection[3] = {25, 63, 63};

bool matrixWriteA = true;  // 'A' is the one being written to
int  matrixRow = 0;

bool processBalance = false;
bool balanceRecieved = false;  // true when we've rx'd a white balance command

byte* wire_DataPtr = NULL;  // the write pointer
int   wire_DataCount = 0;   // bytes received

// timer ISR
byte timerCounter2 = 0;

#ifdef DEMO
unsigned long demoTimeout = 5000UL; // wait 5s after startup with no balance command before running demo.
unsigned long demoTimestamp;
// rolling palette:
unsigned long demoR = 0xFF0000FFUL;
unsigned long demoG = 0xFF00FF00UL;
unsigned long demoB = 0xFFFF0000UL;
unsigned long demoStep = 0;
#endif

inline void i2c_Write(byte value, byte mask)
{
  // I2C write to other chip on this board, msb first
  while (mask)
  {
    if (value & mask)
      SET(I2C_PORT, I2C_SDA);
    else
      CLEAR(I2C_PORT, I2C_SDA);
    mask >>= 1;
    CLEAR(I2C_PORT, I2C_SCL);
    SET(I2C_PORT, I2C_SCL);
  }
}

void wire_Request(void)
{
  // ISR to process request for bytes over I2C from external master
  byte response = wire_DataCount;
  // write MUST be a byte buffer
  Wire.write(&response, 1);
}

void wire_Receive(int numBytes) 
{
  // ISR to process receipt of bytes over I2C from external master
  if (numBytes == 1) // a command
  {
    byte command = Wire.read();
    if (command == 0x00) // start frame
    {
      wire_DataPtr = (matrixWriteA)?matrixDataA:matrixDataB;
      wire_DataCount = 0;
    }
    else if (command == 0x01) // show frame
    {
      matrixWriteA = !matrixWriteA; // atomic
    }
    else if (command == 0x02) // set colour balance
    {
      processBalance = true;
    }
    else if (command == 0x03) // set TCNT2
    {
      byte* ptr = (matrixWriteA)?matrixDataA:matrixDataB;
      if (ptr[1] == 16 && ptr[2] == 128)
         timerCounter2 = *ptr;
    }
    return;
  }
  else if (wire_DataPtr && ((numBytes % MATRIX_CHANNELS) == 0))  // read channel (RGB) data
  {
    while (Wire.available() && wire_DataCount < MATRIX_ROWS*MATRIX_ROW_CHANNELS)
    {
      *(wire_DataPtr++) = Wire.read();
      wire_DataCount++;
    }
  }
  // discard extras
  while (Wire.available())
    Wire.read();
}

// white balance
void setCorrection(byte* pChannel, int dim)
{
  cli();
  CLEAR(LED_PORT, LED_SELECT);  // bank 0, 6-bit
  SET(LED_PORT, LED_LATCH);

  for (int ctr = 0; ctr < MATRIX_COLS; ctr++)
    for (int chan = MATRIX_CHANNELS - 1; chan >= 0; chan--)
      i2c_Write(pChannel[chan] >> dim, 0x20); // 6- bit values
      
  // latches BOTH banks
  CLEAR(LED_PORT, LED_LATCH);
  sei();
}

ISR(TIMER2_OVF_vect)
{
  TCNT2 = timerCounter2;

  // all rows off
  ROW_PORT1 = ROW_PORT2 = 0x00;
  
  SET(LED_PORT, LED_SELECT);  // bank 1, 8-bit
  SET(LED_PORT, LED_LATCH);
  
  byte* pChannel = (!matrixWriteA)?matrixDataA:matrixDataB;  // (READING this time)
  // clock in the channel data for the columns in the current row. REVERSE ORDER!
  pChannel += matrixRow*MATRIX_ROW_CHANNELS + MATRIX_ROW_CHANNELS - 1;
  for (int ctr = 0; ctr < MATRIX_ROW_CHANNELS; ctr++,pChannel--)
    i2c_Write(*pChannel, 0x80);
  
  // latches BOTH banks
  CLEAR(LED_PORT, LED_LATCH);
  
  // turn on this row
  if (matrixRow < 6)
  {
    SET(ROW_PORT1, 0x01 << matrixRow++);
  }
  else if (matrixRow == 6)
  {
    SET(ROW_PORT2, 0x08);
    matrixRow = 7;
  }
  else
  {
    SET(ROW_PORT2, 0x10);
    matrixRow = 0;
  }
}

void setup()
{
  // clear to black
  memset(matrixDataA, 0x00, sizeof(matrixDataA));
  memset(matrixDataB, 0x00, sizeof(matrixDataB));
  
  DDRB = DDRC = DDRD = 0xFF;    // ALL ports to OUTPUT
  ROW_PORT1 = ROW_PORT2 = 0x00; 
    
  // reset the DM163 driver
  CLEAR(LED_PORT, LED_RESET);
  SET(LED_PORT, LED_RESET);

  // default white balance
  setCorrection(defaultCorrection, 0);

  // configure timer2 to service LED row updates at ~800Hz
  ASSR   = _BV(AS2);               // Internal Calibrated clock source 16MHz
  TCCR2A = 0x00;                   // Normal mode
  TCCR2B = _BV(CS22) | _BV(CS20);  // clk/128
  TIMSK2 = _BV(TOIE2);             // Overflow interrupt only

  unsigned long timerFrequencyHz = 800UL; // sweet spot for brightness and steadyness, even when dimmed
  unsigned long clockFrequencyHz = 16000000UL;
  clockFrequencyHz /= 128UL;
  // clock cycles to get the desired period: 
  unsigned long clockCyclesForTimerPeriod = clockFrequencyHz / timerFrequencyHz;
  // overflow the 8-bit counter: 
  timerCounter2 = 255 - clockCyclesForTimerPeriod;
  TCNT2 = timerCounter2;
  sei();
  
  Wire.begin(WIRE_DEVICE_ADDRESS);
  Wire.onRequest(wire_Request);
  Wire.onReceive(wire_Receive);
  
#ifdef DEMO
  demoTimestamp = millis();
#endif  
}

void loop()
{
  if (processBalance)
  {
    setCorrection((matrixWriteA)?matrixDataA:matrixDataB, 0);
    processBalance = false;
    balanceRecieved = true;
  }
#ifdef DEMO
  else if (!balanceRecieved)
  {
    unsigned long now = millis();
    if (now - demoTimestamp >= demoTimeout)
    {
      // kick demo animation
      demoTimestamp = now;
      byte* pChannel = (matrixWriteA)?matrixDataA:matrixDataB;
      if (demoStep < 20)  // first frames are solid/dimmed
        setCorrection(defaultCorrection, 4-(demoStep%5));
      for (int row = 0; row < MATRIX_ROWS; row++)
        for (int col = 0; col < MATRIX_COLS; col++)
        {
          if (demoStep < 20)
          {
            int index = 8*(demoStep/5);
            *(pChannel++) = demoR >> index;
            *(pChannel++) = demoG >> index;
            *(pChannel++) = demoB >> index;
          }
          else
          {
            int index = 8*min(min(MATRIX_ROWS - row - 1, row), 
                              min(MATRIX_COLS - col - 1, col));
            *(pChannel++) = demoR >> index;
            *(pChannel++) = demoG >> index;
            *(pChannel++) = demoB >> index;
          }
        }
      demoStep++;
      matrixWriteA = !matrixWriteA;
      
      if (demoR == 0xFF0000FFUL)
        demoTimeout = 500UL;  // dwell on first frames
      else
        demoTimeout = 200UL;
      if (demoStep > 20)
      {
        demoR = (demoR >> 8) | (random(0xFF) << 24);
        demoG = (demoG >> 8) | (random(0xFF) << 24);
        demoB = (demoB >> 8) | (random(0xFF) << 24);
      }
    }
  }
#endif  
}
