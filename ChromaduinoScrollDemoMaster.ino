#include <Wire.h>
// Example of using I2C to drive a Chromoduino/Funduino LED matrix slave: scrolling text, using faster command (0x11)
// Mark Wilson Jul '18

//********** CONFIGURATION BEGINS
// Things to configure; number of LED matrices, their addresses, colour balances, orientation, command style, speed
#define LED_MATRIX_COUNT (2)  // number of LED matrices connected

// I2C address of the LED matrices.  The first address should be of the matrix connected DIRECTLY to the Master
int matrixAddress[LED_MATRIX_COUNT] = {0x70, 
                                       0x71};

int matrixBalances[LED_MATRIX_COUNT][3] = {{0x80, 0, 0},  // using 0x80 keeps the default
                                           {0x80, 0, 0}};

// if defined, rotates the display 180deg
#define DISPLAY_ROTATED

#define DELAY (150) // milliseconds between each "frame"
//********** CONFIGURATION ENDS

//********** FONT-SPECIFIC CODE BEGINS
#define FONT_COLS (5) // including a gap
#define FONT_ROWS (5)

// This demo is NOT about the font, so use something simple, a compact/chunky 4x5 font
// |<-bit 31                      bit 0->|
// 000D abcd efgh ijkl mnop qrst CCCC CCCC
// glyph:
//  abcd
//  efgh
//  ijkl
//  mnop
//  qrst
// CCCCCCCC = char
// D = descend (not used here)
const unsigned long font_Defn[] PROGMEM =
{
  'A'+0x069F9900, 'B'+0x0E9E9E00, 'C'+0x07888700, 'D'+0x0E999E00, 'E'+0x0F8E8F00, 'F'+0x0F8E8800, 'G'+0x078B9700, 'H'+0x099F9900, 'I'+0x07222700, 'J'+0x07222400, 'K'+0x09ACA900, 'L'+0x08888F00, 'M'+0x09F99900, 
  'N'+0x09DB9900, 'O'+0x06999600, 'P'+0x0E9E8800, 'Q'+0x0699B700, 'R'+0x0E9EA900, 'S'+0x07861E00, 'T'+0x07222200, 'U'+0x09999600, 'V'+0x09996600, 'W'+0x0999F900, 'X'+0x09969900, 'Y'+0x05522200, 'Z'+0x0F124F00, NULL
};

bool font_GetCell(char ch, int row, int col)
{
  // returns true if (row,col) cell is ON in font. (0,0) is top left
  int Idx = 0;
  static unsigned long Defn = 0;  // cache last defn
  if ((unsigned char)(ch) != (Defn & 0xFF))
  {
    do
    {
      Defn = pgm_read_dword(font_Defn + Idx++);
    } while (Defn && (unsigned char)(ch) != (Defn & 0xFF));
  }
  return (Defn && col < 4)?Defn & 0x08000000L >> (row*4 + col):false;
}
//********** FONT-SPECIFIC CODE ENDS

#define RST_PIN A0     // this pin is connected to DTR on the Chromoduino/Funduino

// Arrays representing the LEDs we are displaying.
// Simple on/off here, but could be palette indices
byte Display[LED_MATRIX_COUNT][8][8];


int GetMatrixIndex(int matrix)
{
  // returns the index if the given logical LED matrix
#ifdef DISPLAY_ROTATED    
  return matrix;
#else      
  return LED_MATRIX_COUNT - matrix - 1;
#endif      
}

#define GetMatrixAddress(_m) matrixAddress[GetMatrixIndex(_m)]

void DisplayChar(int matrix, char ch, int row, int col)
{
  // copies the characters font definition into the Buffer at matrix, row, col  
  for (int r = 0; r < FONT_ROWS; r++)
    for (int c = 0; c < FONT_COLS; c++)
      if (0 <= (r+row) && (r+row) < 8 && 8*matrix <= (c+col) && (c+col) < 8*(matrix+1))
        Display[matrix][r+row][(c+col) % 8] = font_GetCell(ch, r, c)?1:0;
}

void StartBuffer(int matrix)
{
  // start writing to WRITE
  Wire.beginTransmission(GetMatrixAddress(matrix));
  Wire.write((byte)0x00);
  Wire.endTransmission();
  delay(1);
}

void StartFastCmd(int matrix)
{
  // start writing to FAST
  Wire.beginTransmission(GetMatrixAddress(matrix));
  Wire.write((byte)0x11);
  Wire.endTransmission();
  delay(1);
}

void WriteData(int matrix, byte R, byte G, byte B)
{
  // write a triple
  Wire.beginTransmission(GetMatrixAddress(matrix));
  Wire.write(R);
  Wire.write(G);
  Wire.write(B);
  Wire.endTransmission();
  delay(1);
}

void ShowBuffer(int matrix)
{
  // flip the buffers
  Wire.beginTransmission(GetMatrixAddress(matrix));
  Wire.write((byte)0x01);
  Wire.endTransmission();
  delay(1);
}

bool SetBalance(int matrix)
{
  // true if there are 3 bytes in the slave's buffer
  Wire.requestFrom(GetMatrixAddress(matrix), 1);
  byte count = 0;
  if (Wire.available())
    count = Wire.read();
    
  Wire.beginTransmission(GetMatrixAddress(matrix));
  Wire.write((byte)0x02); // set the 3 bytes to be balance
  Wire.endTransmission();
  delay(1);
  return count == 3;  // the slave got 3 bytes
}

void SendDisplay(int matrix)
{
  // sends the Display data to the given LED matrix
  // uses a colour and bitmasks
  StartFastCmd(matrix);

  byte data[9];
  for (int row = 0; row < 8; row++)
  {
    data[row+1] = 0x00;
    for (int col = 0; col < 8; col++)
#ifdef DISPLAY_ROTATED    
      if (Display[matrix][7-row][7-col])
#else      
      if (Display[matrix][row][col])
#endif      
        data[row+1] |= 0x01 << col;
  }
  
  int idx = 0;
  data[0] = B00000010;  // write black background, DON'T display at end
  WriteData(matrix, 0, 0, 255);  // blue foreground
  WriteData(matrix, data[idx], data[idx + 1], data[idx + 2]);
  idx += 3;
  WriteData(matrix, data[idx], data[idx + 1], data[idx + 2]);
  idx += 3;
  WriteData(matrix, data[idx], data[idx + 1], data[idx + 2]);
}

bool Configure(int matrix)
{
  // write the balance, true if got expected response from matrix
  StartBuffer(matrix);
  int idx = GetMatrixIndex(matrix);
  WriteData(matrix, matrixBalances[idx][0], matrixBalances[idx][1], matrixBalances[idx][2]);
  return SetBalance(matrix);
}

// the text we're scrolling
char marqueStr[100];
// the logical position of the first char in the display
int marqueCol = 8*(LED_MATRIX_COUNT);

void UpdateDisplay(int matrix)
{
  // updates the display of scrolled text
  memset(Display[matrix], 0, sizeof(Display[matrix]));
  for (int chIdx = 0; chIdx < (int)strlen(marqueStr); chIdx++)
  {
    DisplayChar(matrix, marqueStr[chIdx], (8-FONT_ROWS)/2, marqueCol + chIdx*(FONT_COLS));
  }
}

bool ScrollText()
{
  // shift the text, false if scrolled off
  marqueCol--;
  if (marqueCol < -(int)strlen(marqueStr)*(FONT_COLS))
  {
    // restart
    marqueCol =  8*(LED_MATRIX_COUNT);
    return false;
  }
  return true;
}

void UpdateText()
{
  static bool Hello = true;
  if (Hello)
    strcpy(marqueStr, "HELLO WORLD");
  else
    strcpy(marqueStr, "ATMEL INSIDE");
  Hello = !Hello;
}

void setup()
{
  Wire.begin();
  
  // reset the board(s)
  pinMode(RST_PIN, OUTPUT);
  digitalWrite(RST_PIN, LOW);
  delay(1);
  digitalWrite(RST_PIN, HIGH);

  for (int matrix = 0; matrix < LED_MATRIX_COUNT; matrix++)
  {
    // keep trying to set the balance until it's awake
    do
    {
      delay(100);
    } while (!Configure(matrix));
  }
  UpdateText();
}

void loop()
{
  // update the LEDs
  for (int matrix = 0; matrix < LED_MATRIX_COUNT; matrix++)
  {
    SendDisplay(matrix);
  }
  
  // flip to displaying the new pattern
  for (int matrix = 0; matrix < LED_MATRIX_COUNT; matrix++)
  {
    ShowBuffer(matrix);
  }
  
  delay(DELAY);

  // update the display with the scrolled text
  for (int matrix = 0; matrix < LED_MATRIX_COUNT; matrix++)
  {
    UpdateDisplay(matrix);
  }

  // scroll the text left
  if (!ScrollText())
  {
    // scrolled off: new text
    UpdateText();
  }
}

