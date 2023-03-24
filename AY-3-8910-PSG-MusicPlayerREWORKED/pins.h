#ifndef pins_h
#define pins_h

//------------------
// pins A0 to A7  (analog PORTC)
#ifndef A0
#define A0   (14)
#define A1   (15)
#define A2   (16)
#define A3   (17)
#define A4   (18)
#define A5   (19)
#define A6   (20)
#define A7   (21)
#endif
//-----------------
// pins D0 to D13  (digital PORTD + digital PORTB)
#ifndef D0
#define D0   (0)
#define D1   (1)
#define D2   (2)
#define D3   (3)
#define D4   (4)
#define D5   (5)
#define D6   (6)
#define D7   (7)
#define D8   (8)
#define D9   (9)
#define D10   (10)
#define D11   (11)
#define D12   (12)
#define D13   (13)
#endif
//-----------------

// Assigning Arduino pins
const int DA0_pin         = D0;   // AY38910 DA0
const int DA1_pin         = D1;   // AY38910 DA1
const int DA2_pin         = D2;   // AY38910 DA2
const int DA3_pin         = D3;   // AY38910 DA3
const int DA4_pin         = D4;   // AY38910 DA4
const int DA5_pin         = D5;   // AY38910 DA5
const int DA6_pin         = D6;   // AY38910 DA6
const int DA7_pin         = D7;   // AY38910 DA7

const int BC1_pin         = D8;   // AY38910 BC1
const int AY_Clock_pin    = D9;   // AY38910 CLOCK
const int BDIR_pin        = A2;   // AY38910 BDIR

const int AUDIO_FEEDBACK_A      = A0;   // AY38910 Audio A feedback line
const int AUDIO_FEEDBACK_B      = A1;   // AY38910 Audio B feedback line
const int AUDIO_FEEDBACK_C      = A6;   // AY38910 Audio C feedback line

const int CS_SDCARD_pin   = D10;  // SD card CS (chip select)
const int MOSI_SDCARD_pin = D11;  // SD card MOSI
const int MISO_SDCARD_pin = D12;  // SD card MISO
const int SCK_SDCARD_pin  = D13;  // SD card SCK
const int ResetAY_pin     = A3;   // AY38910 RESET
const int NextButton_pin  = A7;   // user input
// SDA=A4,SCL=A5   128x32 i2c OLED - 0.96".

#endif  
