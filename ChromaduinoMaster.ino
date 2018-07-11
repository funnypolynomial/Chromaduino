#include <Wire.h>
// Example of using I2C to drive a Chromaduino/Funduino LED matrix slave
// Just cycles colours
// Mark Wilson Dec '16

#define I2C_ADDR 0x05  // the Chromaduino/Funduino I2C address
#define RST_PIN A0     // this pin is connected to DTR on the Chromaduino/Funduino

byte balanceRGB[3] = {22, 63, 63};
byte colourRGB[3] = {255, 0, 0};

void StartBuffer()
{
  // start writing
  Wire.beginTransmission(I2C_ADDR);
  Wire.write((byte)0x00);
  Wire.endTransmission();
  delay(1);
}

void WriteRGB(byte* pRGB)
{
  // write a triple
  Wire.beginTransmission(I2C_ADDR);
  for (int idx = 0; idx < 3; idx++, pRGB++)
    Wire.write(*pRGB);
  Wire.endTransmission();
  delay(1);
}

void ShowBuffer()
{
  // flip the buffers
  Wire.beginTransmission(I2C_ADDR);
  Wire.write((byte)0x01);
  Wire.endTransmission();
  delay(1);
}

bool SetBalance()
{
  // true if there are 3 bytes in the slave's buffer
  Wire.requestFrom(I2C_ADDR, 1);
  byte count = 0;
  if (Wire.available())
    count = Wire.read();
    
  Wire.beginTransmission(I2C_ADDR);
  Wire.write((byte)0x02); // set the 3 bytes to be balance
  Wire.endTransmission();
  delay(1);
  return count == 3;  // the slave got 3 bytes
}

void DisplayBuffer()
{
  // fill the buffer and display it
  StartBuffer();
  for (int row = 0; row < 8; row++)
    for (int col = 0; col < 8; col++)
      WriteRGB(colourRGB);
  ShowBuffer();
}

bool Configure()
{
  // write the balance, true if got expected response from matrix
  StartBuffer();
  WriteRGB(balanceRGB);
  return SetBalance();
}

void swap(byte& a, byte& b)
{
  byte t = a;
  a = b;
  b = t;
}

void setup()
{
  Wire.begin();
  
  // reset the board
  pinMode(RST_PIN, OUTPUT);
  digitalWrite(RST_PIN, LOW);
  delay(1);
  digitalWrite(RST_PIN, HIGH);
  
  // keep trying to set the slave's balance until it's awake
  do
  {
    delay(100);
  } while (!Configure());
}

void loop()
{
  DisplayBuffer();
  delay(1000);
  swap(colourRGB[0], colourRGB[1]);
  swap(colourRGB[1], colourRGB[2]);
}

