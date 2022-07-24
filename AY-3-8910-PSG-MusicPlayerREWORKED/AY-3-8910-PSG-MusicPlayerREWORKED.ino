// This sketch plays PSG files via a AY-3-8910 which is a 3-voice programmable sound generator (PSG)
// Code has been optimised (mainly to save space) and will now fit on a 'ATmega168'

//TODO .. add more user inputs/buttons, play, next, back, random tune ...
//TODO .. send audio into Arduino / use analogRead /  reading for a VU meter
//TODO .. maybe use IO on AY chip as a VU Meter
//TODO ... SD CARD WIH NO MUSIC
//TODO ... [DONE] REWORK HARDWARE - lose chip for Serial to Parallel Shifting
//TODO ... [DONE] lose fat.h and use SD.h - changeover was easier than I was expecting

#include <SPI.h>
#include <SD.h>  // with this lib, works on LGT8F328P ... needed to stop using fat.h and use SD.h
#include "SSD1306Ascii.h"
#include "SSD1306AsciiAvrI2c.h"
#include "fudgefont.h"  // Based on the Adafruit5x7 font, with '!' to '(' changed to work as a VU BAR (8 chars)
#include "pins.h"
#include "AY3891xRegisters.h"

#define VERSION ("1.1")


// ********************************************************************
// Pick one of thse two BUFFER_SIZE options you are compiling for.
// ********************************************************************
//
// OPTION 1:   - ATmega328(2K) allows optimised counters
// ========
#define BUFFER_SIZE (256)
//
// OPTION 2:   - ATmega168(1K) code uses a smaller buffer
// ========
//#define BUFFER_SIZE (64)
//
// ********************************************************************

byte playBuf[BUFFER_SIZE];  // PICK ONE OF THE ABOVE BUFFER SIZES

// NOTE: The ATmega168 is a cut-down pro mini with JUST 1k, Atmega238P has 2k.
// Compiling ATmega168 with a 256 BUFFER_SIZE (OPTION 1), results are:
//    "Sketch uses 12602 bytes (87%) of program storage space. Maximum is 14336 bytes."
//    "Global variables use 1009 bytes (98%) of dynamic memory, leaving 15 bytes for local variables. Maximum is 1024 bytes."
// Without doing something drastic the ATmega168 ONLY HAS 15 bytes free!
// USE OPTION 2 / TURN DOWN THE CACHE SIZE DOWN TO 64 BYTES ON THE 1K VERSION

#if (BUFFER_SIZE==256)
#define ADVANCE_PLAY_BUFFER  circularBufferReadIndex++;
#define ADVANCE_LOAD_BUFFER  circularBufferLoadIndex++;
#else
#define ADVANCE_PLAY_BUFFER  circularBufferReadIndex++; if (circularBufferReadIndex>=BUFFER_SIZE) {circularBufferReadIndex=0;}
#define ADVANCE_LOAD_BUFFER  circularBufferLoadIndex++; if (circularBufferLoadIndex>=BUFFER_SIZE) {circularBufferLoadIndex=0;}
#endif

#define RESET_LOADPLAY_BUFFER circularBufferLoadIndex = circularBufferReadIndex = 0;

// PSG commands - Incoming byte data from file
#define  END_OF_INTERRUPT_0xFF            (0xff)
#define  END_OF_INTERRUPT_MULTIPLE_0xFE   (0xfe)
#define  END_OF_MUSIC_0xFD                (0xfd)

// AY modes for BDIR and BC1
// NOTE: BC2 tied to +5v
enum AYMode { INACTIVE, WRITE, LATCH_ADDRESS };
// ----------------------------------
// BDIR   BC2   BC1   PSG FUNCTION
// ----+-----+-----+-----------------
// 0      1      0    INACTIVE
// 0      1      1    READ FROM PSG (not needed)
// 1      1      0    WRITE TO PSG
// 1      1      1    LATCH ADDRESS
// ----------------------------------

// Arduino Timer 2
// Aiming for PWM of 50HZ, so interrupt triggers every 20ms
// 62500MHz / duty 250 = 250 interrupts per second (so will scale by 5 in ISR code)
const byte dutyCycleTimer2 = 250;   // note: timer 2 has a 8 bit resolution

#define I2C_ADDRESS 0x3C  // 0x3C or 0x3D
SSD1306AsciiAvrI2c oled;
// Display Character Rows
#define DISPLAY_ROW_FILENAME        (0)
#define DISPLAY_ROW_FILE_COUNTER    (1)
#define DISPLAY_ROW_BYTES_LEFT      (1)
#define DISPLAY_ROW_VU_METER_TOP    (2)
#define DISPLAY_ROW_VU_METER_BOTTOM (3)
// Maths scale used for internals of VU meter
#define VU_METER_INTERNAL_SCALE (2)  // speed scale, must use 2^n values

File root;
File file;

enum  { FLAG_NEXT_TUNE, FLAG_BACK_TUNE, FLAG_PLAY_TUNE, FLAG_BUTTON_DOWN, FLAG_BUTTON_REPEAT, FLAG_REFRESH_DISPLAY }; // 8 or less items here (byte)
volatile byte playFlag;

int filesCount = 0;
int fileIndex = 0;            // file indexes start from zero
volatile int interruptCountSkip = 0;   // Don't play new sequences via interrupt when this is positive (do nothing for 20ms)
uint32_t fileSize;

byte circularBufferLoadIndex;  // Optimied : this counter wraps back to zero by design (using 256 buffer option)
byte circularBufferReadIndex;  // Optimied : this counter wraps back to zero by design
byte volumeChannelA = 0;
byte volumeChannelB = 0;
byte volumeChannelC = 0;


// optimise
volatile int next = 0;
int buttonwait = 0;
byte count = 0; // =B10000000;

extern void setupPins();
extern void resetAY();
extern  void setupClockForAYChip();
extern void setupOled();
extern int countPlayableFiles();
extern void setupProcessLogicTimer();
extern void selectFile(int index);
extern void cacheSingleByteRead();
extern void displayVuMeterTopPar(byte volume);
extern void displayVuMeterBottomPar(byte volume);
extern bool isCacheReady();
extern void setAYMode(AYMode mode);

// startup the display, audit files, fire-up timers at 1.75 MHz'ish, reset AY chip
void setup() {

  // Serial.begin(9600);   // this library eats 177 bytes, remove from release
  //Serial.println(sizeof(SdFat));
  // NOW USING TX, RX LINES .. SO USING SERIAL WILL MESS WITH OPERATIONS

  setupPins();
  resetAY();
  setupOled();

SD_CARD_MISSING_RETRY:
  delay(1500);  // time to read the VERSION

  if (SD.begin(CS_SDCARD_pin)) {
    root = SD.open("/");
    filesCount = countPlayableFiles();
  } else {
    oled.println(F("Waiting for SD card"));
    goto SD_CARD_MISSING_RETRY;
  }

  selectFile(fileIndex);
  // pre fill cache, give things a head start.
  for (int i = 0; i < BUFFER_SIZE; i++ ) {
    cacheSingleByteRead();
  }

  setupClockForAYChip();
  setupProcessLogicTimer(); // start the logic interrupt up last

  bitSet(playFlag, FLAG_PLAY_TUNE);
  bitSet(playFlag, FLAG_REFRESH_DISPLAY);
}

void loop() {
  if  (bitRead(playFlag, FLAG_REFRESH_DISPLAY)) {
    int but = analogRead(NextButton_pin);
    if (but > 2700)
      bitSet(playFlag, FLAG_BACK_TUNE);
    else if (but > 2000)
      bitSet(playFlag, FLAG_NEXT_TUNE);

    if (count == 0 || but < 100) {
      if (bitRead(playFlag, FLAG_NEXT_TUNE)) {
        bitClear(playFlag, FLAG_NEXT_TUNE);
        if (++fileIndex >= filesCount) {
          fileIndex = 0;
        }
        bitSet(playFlag, FLAG_PLAY_TUNE);
      }
      if (bitRead(playFlag, FLAG_BACK_TUNE)) {
        bitClear(playFlag, FLAG_BACK_TUNE);
        if (--fileIndex < 0 ) {
          fileIndex = filesCount - 1;
        }
        bitSet(playFlag, FLAG_PLAY_TUNE);
      }

      if (bitRead(playFlag, FLAG_PLAY_TUNE)) {
        bitClear(playFlag, FLAG_PLAY_TUNE);
        resetAY();
        
        RESET_LOADPLAY_BUFFER
        
        selectFile(fileIndex);
        oled.setCursor(0, DISPLAY_ROW_FILENAME);
        
        //Since the SD source code has:  char _name[13]; then later on uses strncpy(_name, n, 12); _name[12] = 0;
        for (int i=0; i < (12-strlen(file.name())) ; i++) {
          file.name()[12-1-i]= ' ';  //  HACK... this kind of all fine, don't panic
        }   
        oled.print((char*)file.name());  // becase of the above fudge, this will disaply all 12 characters - so will clear old shorter names
      }
      
      oled.setCursor(0, DISPLAY_ROW_FILE_COUNTER);
      oled.print(fileIndex + 1);
      oled.print(F("/"));
      oled.print(filesCount);
    }
    
    oled.setCursor((128 / 2) - 6 - 6 - 6, DISPLAY_ROW_VU_METER_TOP);
    displayVuMeterTopPar(volumeChannelA / VU_METER_INTERNAL_SCALE); // dividing by 2, scaled maths used (*2 scale used setting VU meter)
    displayVuMeterTopPar(volumeChannelB / VU_METER_INTERNAL_SCALE);
    displayVuMeterTopPar(volumeChannelC / VU_METER_INTERNAL_SCALE);

    oled.setCursor((128 / 2) - 6 - 6 - 6, DISPLAY_ROW_VU_METER_BOTTOM);
    displayVuMeterBottomPar(volumeChannelA / VU_METER_INTERNAL_SCALE);
    displayVuMeterBottomPar(volumeChannelB / VU_METER_INTERNAL_SCALE);
    displayVuMeterBottomPar(volumeChannelC / VU_METER_INTERNAL_SCALE);

    oled.setCursor(128 - 32, DISPLAY_ROW_BYTES_LEFT);
    oled.print(fileSize / 1024);
    oled.print("K ");

    volumeChannelA--;  // drift all the VU meters down over time (values are internally scaled)
    volumeChannelB--;
    volumeChannelC--;

    bitClear(playFlag, FLAG_REFRESH_DISPLAY);
    count -= (256 / 32); // letting byte wrap
  }
  cacheSingleByteRead();  //cache more music data if needed
}

// Generate two bus control signals for AY/PSG pins (BDIR and BC1) over the port
// PORTB maps to Arduino digital pins 8 to 13 (PB0 to PB5)
// pins (PB6 and PB7) are not available ( 16MHz crytsal is connected with XTAL2/PB6 and XTAL1/PB7 )
void setAYMode(AYMode mode) {
  switch (mode) {
    case INACTIVE:  //PORTB &= _BV(PB7)|_BV(PB6)|_BV(PB5)|_BV(PB4)|_BV(PB3)|_BV(PB2); break;  // (B11111100)
      digitalWrite(BDIR_pin, LOW);
      digitalWrite(BC1_pin, LOW);
      break;
    case WRITE:     //PORTB |= _BV(PB1); break;             // output: pin 9     (B00000010)
      digitalWrite(BDIR_pin, HIGH);
      digitalWrite(BC1_pin, LOW);
      break;
    case LATCH_ADDRESS:    // PORTB |= _BV(PB1)|_BV(PB0);  break;   // output: pins 8,9  (B00000011)
      digitalWrite(BDIR_pin, HIGH);
      digitalWrite(BC1_pin, HIGH);
      break;
  }
}

//-------------------------------------------------------------------------------------------------
//  Operation                    Registers       Function
//--------------------------+---------------+------------------------------------------------------
// Tone Generator Control        R0 to R5        Program tone periods
// Noise Generator Control       R6              Program noise period
// Mixer Control                 R7              Enable tone and/or noise on selected channels
// Amplitude Control             R8 to R10       Select "fixed" or "envelope-variable" amplitudes
// Envelope Generator Control    R11 to R13      Program envelope period and select envelope pattern
//-------------------------------------------------------------------------------------------------
// Send latching data via 74HC595 to AY chip + Update VU meter
// (74HC595 used as limited amount of pins available on a Arduino pro mini)
// NOTE: *** Used by interrupt, keep code lightweight ***
void writeAY( byte port , byte ctrl ) {
  if (port < PSG_REG_TOTAL) {
    setAYMode(LATCH_ADDRESS);
    PORTD = port;
 //   digitalWrite(DA3_pin, port & B00001000);
    setAYMode(INACTIVE);

    setAYMode(WRITE);
    PORTD = ctrl;
   // digitalWrite(DA3_pin, control & B00001000);
    setAYMode(INACTIVE);

    switch (port) {
      case PSG_REG_LVL_A: volumeChannelA = ctrl * VU_METER_INTERNAL_SCALE; break; // *2 for scaled maths (VU meter speed)
      case PSG_REG_LVL_B: volumeChannelB = ctrl * VU_METER_INTERNAL_SCALE; break;
      case PSG_REG_LVL_C: volumeChannelC = ctrl * VU_METER_INTERNAL_SCALE; break;
      case PSG_REG_ENV_SHAPE: if (ctrl == 255) return; // Envelope bugfix ???? NOT TESTED
    }
  }
}

// PSG music format (body)
// [0xff]              : End of interrupt (EOI) - waits for 20 ms
// [0xfe],[byte]       : Multiple EOI, following byte provides how many times to wait 80ms.
// [0xfd]              : End Of Music
// [0x00..0x0f],[byte] : PSG register, following byte is accompanying data for this register
// (Again... This method need to be lightweight as it's part of the interrupt)
void playNotes() {
  while (isCacheReady()) {
    byte b = playBuf[circularBufferReadIndex];
    ADVANCE_PLAY_BUFFER
    switch (b) {
      case END_OF_INTERRUPT_0xFF: return;
      case END_OF_INTERRUPT_MULTIPLE_0xFE:
        if (isCacheReady()) {
          b = playBuf[circularBufferReadIndex];
          ADVANCE_PLAY_BUFFER

          if ((b == 0xff) && (fileSize / 32 == 0) )  {
            // Some tunes have very long pauses at the end (caused by repeated sequences of "0xfe 0xff").
            // For example "NewZealandStoryThe.psg" has a very long pause at the end, I'm guessing by design to handover to the ingame tune.
            interruptCountSkip = 4; // 4 works well for me! Forcing shorter pauses, but only when nearing the end of the tune and its FF
            resetAY();
            // keeps doing this special timing adjustment for the last 32 bytes read
          }
          else {
           // if (!bitRead(playFlag, FLAG_BUTTON_REPEAT)) {
              interruptCountSkip = b << 2; //   x4, to make each a 80 ms wait
         //   }
          }
          return;
        } else {
          // cache not ready, need to wait a bit. Rewinding back to the starting command.
          circularBufferReadIndex -= 2; // canceling  that last advance
        }
        break;
      case END_OF_MUSIC_0xFD:     bitSet(playFlag, FLAG_NEXT_TUNE);  return;
      default:  // 0x00 to 0xFC
        if (isCacheReady()) {
          writeAY(b, playBuf[circularBufferReadIndex]);
          ADVANCE_PLAY_BUFFER
        } else {
          // cache not ready, need to wait a bit. Rewinding back to the starting command.
          circularBufferReadIndex -= 2; // canceling  that last advance
        }
        break;
    }
  }
}

// Q: Why top and bottom functions?
// A: Two character are joined to make a tall VU meter.
inline void displayVuMeterTopPar(byte volume) {
  if (volume >= 8) {
    // Note: x8 characters have been redefined for the VU memter starting from '!'
    oled.print( (char) ('!' + (((volume) & 0x07)) ) );
    oled.print( (char) ('!' + (((volume) & 0x07)) ) );
  }
  else {
    oled.print( F("  ") );  // nothing to show, clear.  (F() puts text into program mem)
  }
}
inline void displayVuMeterBottomPar(byte volume) {
  if (volume < 8) {
    // Note: x8 characters have been redefined for the VU memter starting from '!'
    oled.print( (char) ('!' + (((volume) & 0x07)) ) );
    oled.print( (char) ('!' + (((volume) & 0x07)) ) );
  }
  else {
    oled.print( F("(("));  // '(' is redefined as a solid bar for VU meter
  }
}

// Returns true when data is waiting and ready on the cache.
inline bool isCacheReady() {
  return circularBufferReadIndex != circularBufferLoadIndex;
}

void setupOled() {
  oled.begin(&Adafruit128x32, I2C_ADDRESS);
  delay(1);
  // some hardware is slow to initialise, first call does not work.
  oled.begin(&Adafruit128x32, I2C_ADDRESS);
  // original Adafruit5x7 font with tweeks at start for VU meter
  oled.setFont( fudged_Adafruit5x7 );
  oled.clear();
  oled.print(F("ver"));
  oled.println(F(VERSION));
}

// Arduino (Atmega) pins default to INPUT (high-impedance state)
// Configure the direction of the digital pins. (pins default to INPUT at power up)
void setupPins() {

  // Pins 0 to 7 used for AY data however pin 3 'D3' is taken.
  // So need one more bit elsewhere, pin 'A2' is used to fill in for that missing bit.
  //      -----x--
 // DDRD  = B11111111;     // set pins 0 to 7 as OUTPUT
 // PORTD = B11111011;            // clear AY byte Data pins 0,1,x,3,4,5,6,7

  DDRD = 0xff;
  PORTD =0;

 pinMode(D9, OUTPUT);


  pinMode(A2, OUTPUT);
 // pinMode(A2, OUTPUT);  // AY data pin 3 is sent via pin 'A2'

  // Assign pins for the AY Chip
  pinMode(ResetAY_pin, OUTPUT);
  pinMode(BC1_pin, OUTPUT);
  pinMode(BDIR_pin, OUTPUT);

  // Assign pins for the user input
  pinMode(NextButton_pin, INPUT);

  // SD card
  pinMode(CS_SDCARD_pin, OUTPUT);
  pinMode(MOSI_SDCARD_pin, INPUT);
  pinMode(MISO_SDCARD_pin, INPUT);
  pinMode(SCK_SDCARD_pin, INPUT);

}

// Three Arduino Timers 
// Timer0: 8 bits; Used by libs, e.g millis(), micros(), delay()
// Timer1: 16 bits; Use by Servo, VirtualWire and TimerOne library
// Timer2: 8 bits; Used by the tone() function



ISR(TIMER2_COMPA_vect) {
//    static volatile byte ddd=0;   // DEBUG TEST ... REMOVE ME
  static volatile byte ScaleCounter=0;
  // 50 Hz, 250 interrupts per second / 50 = 5 steps per 20ms 
  if (++ScaleCounter >= (dutyCycleTimer2 /50) ) {

    if (interruptCountSkip>0){
 //     oled.setCursor(80, 0);
//      oled.print(ddd++);
      interruptCountSkip--;
    }else{
      playNotes();
    }
    ScaleCounter=0;
    bitSet(playFlag, FLAG_REFRESH_DISPLAY); 
  }
}


//Each Timer/Counter has two output compare pins.
//Timer/Counter 0 OC0A and OC0B pins are called PWM pins 6 and 5 respectively.
//Timer/Counter 1 OC1A and OC1B pins are called PWM pins 9 and 10 respectively.
//Timer/counter 2 OC2A and OC2B pins are called PWM pins 11 and 3 respectively.


// The AY38910 clock pin
// The ZX Spectrum's 128K's AY soundchip is fed with a 1.7734MHz clock  (speccy Z80 CPU 3.5469/2=1.77345)       
void setupClockForAYChip() {
  TCCR1A = bit(COM1A0);
  TCCR1B = bit(WGM12) | bit(CS10);
  OCR1A = 3;   // set a 2MHz frequence
  pinMode(9, OUTPUT);
}


// Setup 8-bit timer2 to trigger interrupt (see ISR function)
void setupProcessLogicTimer() {

// History: Calculating prescaler and duty 
// Looking for any of these to be a multiple of 50HZ
//  15625   1024 ...  1, 5, 25, 125 ... NO - nothing here fits my 50HZ target
//  62500    256 ...  1, 2, 4, 5, 10, 20, 25, 50, 100, 125, >>>> 250 WINNER <<<<<
//  125000   128 ...  1, 2, 4, 5, 8, 10, 20, 25, 40, 50, 100, 125, 200, 250 ... NOPE To fast
//  250000    64 ...  1, 2, 4, 5, 8, 10, 16, 20, 25, 40, 50, 80, 100, 125, 200, 250 - NOPE, yeah way to fast 

  cli();                            // Disable interrupts

  TCCR2A = _BV(WGM21);              // CTC mode (Clear Timer and Compare)             
  // 16000000 (ATmega16MHz) / 256 prescaler = 62500MHz
  // 62500MHz Timer Clock =  16000000MHz / 256
  TCCR2B =  _BV(CS22) | _BV(CS21);  // 256 prescaler  (CS22=1,CS21=1,CS20=0)
  OCR2A = dutyCycleTimer2;          // Set the compare value to control duty cycle                  
  TIFR2 |= _BV(OCF2A);              // Clear pending interrupts  
  TIMSK2 = _BV(OCIE2A);             // Enable Timer 2 Output Compare Match Interrupt      
  TCNT2 = 0;                        // Timer counter 2
  
  sei();                            // enable interrupts

  // https://onlinedocs.microchip.com/pr/GUID-93DE33AC-A8E1-4DD9-BDA3-C76C7CB80969-en-US-2/index.html?GUID-669CCBF6-D4FD-4E1D-AF92-62E9914559AA
  
}

// Skip header information
// Example of Header(16 bytes) followed by the start of the raw byte data
// HEADER: 50 53 47 1A 00 00 00 00 00 00 00 00 00 00 00 00
// DATA  : FF FF 00 F9 06 16 07 38 FF 00 69 06 17 FF 00 F9 ...
//         ^^ ^^
inline int advancePastHeader() {
  file.seek(16); // absolute position
  return 16;
}

// Here we are checking for the "PSG" file header, byte by byte.
// note: if found, global file will have advanced 3 bytes
bool isFilePSG() {
  if (file) {
    if (!file.isDirectory()) {
      // Doing every little bit to save some dynamic memory.
      // Could put "PSG" in program mem, but that would still require a counter eating away at the stack (yes one byte but I'm very low on stack/memory)
     return (file.available() && file.read() == 'P' && file.available() &&  file.read() == 'S' && file.available() && file.read() == 'G');
    }
  }
  return false;
}

int countPlayableFiles() {
  int count=0;
  while (true) {
    file = root.openNextFile();
    if (!file) {
      break;
    }
    if (isFilePSG()) {
      count++;
    }
    file.close();
  }
  root.rewindDirectory();
  return count;
}

void selectFile(int fileIndex) { // optimise

  if (file) {
    file.close();
  }

  if (root) {
    root.rewindDirectory();
    int k = 0;
    file = root.openNextFile();
    while (file) {

      if (isFilePSG()) {
        if (k == fileIndex) {
          fileSize = file.size();
          if (fileSize > 16) { // check we have a body, 16 PSG header
            // oled.setCursor(0, DISPLAY_ROW_FILENAME);
            //  oled.print((char*)file.name());
            fileSize -= advancePastHeader();
            break;  // Found it - leave this file open, cache takes over from here on and process it.
          }
        }
        k++;
      }

      file.close(); // This isn't the file you're looking for, continue looking.
      file = root.openNextFile();
    }
  }
}

// Reads a single byte from the SD card into the cache (circular buffer)
// Anything after EOF sets a END_OF_MUSIC_0xFD command into the cache
// which will trigger a FLAG_PLAY_NEXT_TUNE.
void cacheSingleByteRead() {
  if (circularBufferLoadIndex == circularBufferReadIndex - 1)  // cache is behind, wait for it to refill
    return ;
  if (circularBufferLoadIndex == (BUFFER_SIZE - 1) && circularBufferReadIndex == 0)  // cache is behind
    return ;

  if (fileSize >= 1) { // file.available()) {
    playBuf[circularBufferLoadIndex] =  file.read();
    fileSize--;
  }
  else {
    playBuf[circularBufferLoadIndex] = END_OF_MUSIC_0xFD;
  }
  ADVANCE_LOAD_BUFFER
}


// Reset AY chip to stop sound output
// Reset line needs to go High->Low->High for AY38910/12
// Reset pulse width must be 500ns (min), this comes from the AY38910/12 datasheet.
void resetAY() {
  setAYMode(INACTIVE);
  digitalWrite(ResetAY_pin, HIGH); // best set this, for first time
  delay(1);
  digitalWrite(ResetAY_pin, LOW);
  delay(1);
  digitalWrite(ResetAY_pin, HIGH);
  setAYMode(INACTIVE);
}





void loop_TEST() {

  // Test the file handling
  // make sure everthing works and closes files and stuff
  //
  // leave running - if it does not lock up then the logic is "probably" ok.
  //
  for (;;) {

    selectFile(fileIndex);
    if (++fileIndex >= filesCount) {
      fileIndex = 0;
    }
    oled.setCursor(0, DISPLAY_ROW_FILENAME);
    oled.print((char*)file.name());
  }
}


/*
   # SCRATCH PAD IGNORE ...

  filesCount =3;
  for(;;) {
   if  (refresh) {
    root.rewindDirectory();
    while (true) {
    File entry = root.openNextFile();
     if (!entry) { break;}
     else {

      if (!entry.isDirectory()) {

        oled.setCursor(0, DISPLAY_ROW_FILENAME);
        oled.print((char*)entry.name());
       }
       entry.close();
       refresh = false;
    }
    }
   }
  }
*/


 // 730, 2040, 2800, 4200
  // 22 , 326, 515, 842
