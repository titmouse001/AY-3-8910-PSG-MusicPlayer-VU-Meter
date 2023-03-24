// This sketch plays PSG files via a AY-3-8910 which is a 3-voice programmable sound generator (PSG)
// Code has been optimised (mainly to save space) and will now fit on a 'ATmega168'

//TODO .. add more user inputs/buttons, play, next, back, random tune ...
//TODO .. send audio into Arduino / use analogRead /  reading for a VU meter
//TODO .. maybe use IO on AY chip as a VU Meter
//TODO ... SD CARD WIH NO MUSIC
//TODO ... [DONE] REWORK HARDWARE - lose chip for Serial to Parallel Shifting
//TODO ... [DONE] lose fat.h and use SD.h - changeover was easier than I was expecting



// "logic green nano"  compile using these settings:  board='LGT8F328' | clock=16Mz | variant=328P-LQFP48 mini-EVB
// Only have Arduino IDE 2.0 with this installed


// Arduino (Nano) Pins used:
// PINS:
// D0 to D7 ->  AY-3-8910 data pins  DA0 to DA7
// D8       -> AY38910 BC1
// D9       -> AY38910 CLOCK
// D10      -> SD card CS (chip select)
// D11      -> SD card MOSI
// D12      -> SD card MISO
// D13      -> SD card SCK
// A0       -> AY38910 Audio A feedback line
// A1       -> AY38910 Audio B feedback line
// A2       -> AY38910 BDIR
// A3       -> AY38910 RESET
// A4       -> i2c bus 'SCL'  (single OLED on bus as '0x3C')
// A5       -> i2c bus 'SDA'  (single OLED on bus as '0x3C') 
// A6       -> AY38910 Audio C feedback line
// A7       -> user input


#include <SPI.h>
#include <SD.h>  // with this lib, works on LGT8F328P ... needed to stop using fat.h and use SD.h
#include "SSD1306Ascii.h"
#include "SSD1306AsciiAvrI2c.h"
#include "fudgefont.h"  // Based on the Adafruit5x7 font, with '!' to '(' changed to work as a VU BAR (8 chars)
#include "pins.h"
#include "AY3891xRegisters.h"

#define VERSION ("2.3")


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
enum AYMode { INACTIVE,READ, WRITE, LATCH_ADDRESS };
// ----------------------------------
// BDIR   BC2   BC1   PSG FUNCTION
// ----+-----+-----+-----------------
// 0      1      0    INACTIVE
// 0      1      1    READ FROM PSG
// 1      1      0    WRITE TO PSG
// 1      1      1    LATCH ADDRESS
// ----------------------------------

// Arduino Timer2  (8-bit timer)
// Aiming for PWM of 50HZ, so interrupt triggers every 20ms if possible
//
const byte DUTY_CYCLE_FOR_TIMER2 = 250;   // note: timer 2 has a 8 bit resolution
//
// History: for calculating the prescaler and duty
//
// Looking for any of these to be a multiple of 50HZ
//1024: (16000000Hz / 1024 prescale) = 15625Hz
//   gives factors 1, 5, 25, 125 ... NO - nothing here fits my 50HZ target i.e. (125/50 = 2.5)
//256:  (16000000Hz / 256 prescale)  = 62500Hz
//   gives factors 1, 2, 4, 5, 10, 20, 25, 50, 100, 125, 250                        >>>> 250 WINNER <<<<<
//128:  (16000000Hz / 128 prescale)  = 125000Hz
//   gives factors 1, 2, 4, 5, 8, 10, 20, 25, 40, 50, 100, 125, 200, 250 ... NOPE To fast
// 64:  (16000000Hz / 64 prescale)   = 250000Hz
//   gives factors 1, 2, 4, 5, 8, 10, 16, 20, 25, 40, 50, 80, 100, 125, 200, 250 - NOPE, yeah way to fast
//
// 16000000Hz / 256  = 62500Hz
// 62500Hz / duty 250 = 250 interrupts per second (so will scale by 5 in ISR code)
//
#define I2C_ADDRESS 0x3C  // 0x3C or 0x3D
SSD1306AsciiAvrI2c oled;
// Display Character Rows
#define DISPLAY_ROW_FILENAME        (0)
#define DISPLAY_ROW_FILE_COUNTER    (1)
#define DISPLAY_ROW_BYTES_LEFT      (1)
#define DISPLAY_ROW_VU_METER_TOP    (2)
#define DISPLAY_ROW_VU_METER_BOTTOM (3)
// Maths scale used for internals of VU meter
//#define VU_METER_INTERNAL_SCALE (2)  // speed scale, must use 2^n values
#define VU_METER_INTERNAL_SCALE (1)  // speed scale, must use 2^n values

File root;  // 'File' struct takes 27 bytes
File file;

enum  { FLAG_NEXT_TUNE, FLAG_BACK_TUNE, FLAG_PLAY_TUNE, FLAG_BUTTON_REPEAT, FLAG_REFRESH_DISPLAY }; // 8 or less items here (byte)
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
byte volumeChannelA_Prev = 0;
byte volumeChannelB_Prev = 0;
byte volumeChannelC_Prev = 0;

static byte  LastAYEnableRegisiterUsed=0 ;  // lets use set I/O ports without stumping over the AY's enabled sound bits

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
extern void selectFile(byte index);
extern void cacheSingleByteRead();
extern void displayVuMeterTopPar(byte volume);
extern void displayVuMeterBottomPar(int volume);
extern bool isCacheReady();
extern void setAYMode(AYMode mode);

int baseAudioVoltage = 0; // initialised once in setup for VU meter - lowest point posible
int topAudioVoltage = 0; // for VU meter stepup - highest point posible

volatile int audioAsum = 0;
volatile int audioBsum = 0;
volatile int audioCsum = 0;
volatile int audioMeanA = 0;
volatile int audioMeanC = 0;
volatile int audioMeanB = 0;




// startup the display, audit files, fire-up timers at 1.75 MHz'ish, reset AY chip
void setup() {

  // Serial.begin(9600);   // this library eats 177 bytes, dont forget to remove from release!!!
  // Serial.println(sizeof(SdFat));
  // NOTE: NOW USING TX, RX LINES .. using serial debug will mess things up, to use debug first disable PORTD writes in writeAY()
  
  setupOled();
  setupPins();
  resetAY();

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

  oled.clear();

  // Sample AY audio lines (x3) for a good starting signal
  baseAudioVoltage = analogRead(AUDIO_FEEDBACK_A);
  baseAudioVoltage = min(baseAudioVoltage, analogRead(AUDIO_FEEDBACK_B));
  baseAudioVoltage = min(baseAudioVoltage, analogRead(AUDIO_FEEDBACK_C));

}

void loop() {

  if  (bitRead(playFlag, FLAG_REFRESH_DISPLAY)) {

    int but = analogRead(NextButton_pin);

    // When running from battery power this measured voltage can sag at little 
    if (but > 4000-400)  // generous value allows for battery power
      int g;
    else if (but > 3400-200)
      bitSet(playFlag,  FLAG_NEXT_TUNE );
    else if (but > 2000-200)
      bitSet(playFlag, FLAG_BACK_TUNE);
    else if (but > 1300- 200)
      bitSet(playFlag, FLAG_NEXT_TUNE); // temp
    else 
      bitSet(playFlag, FLAG_BACK_TUNE); // temp
     
    if (count == 0 || but > 4000) {
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

        // NOTE: Since the SD source code has:  char _name[13]; then later on uses strncpy(_name, n, 12); _name[12] = 0;
        const byte blankArea =  strlen(file.name());
        memset( file.name() + blankArea, ' ', 12 - blankArea );  // clear last few charactes, alowing for shorter names
        oled.print((char*)file.name());  // becase of the above fudge, this will disaply all 12 characters - so will clear old shorter names
      }

      oled.setCursor(0, DISPLAY_ROW_FILE_COUNTER);
      oled.print(fileIndex + 1);
      oled.print(F("/"));
      oled.print(filesCount);
      oled.print(F(" "));
    }

    // find largest top end
    topAudioVoltage = max(topAudioVoltage, audioMeanA);
    topAudioVoltage = max(topAudioVoltage, audioMeanB);
    topAudioVoltage = max(topAudioVoltage, audioMeanC);
    // Allow audio to dip over time
    topAudioVoltage--;   // works in synergy with the above max lines
    // Note: At this point audio data has been scaled to fit into a byte
    // scale down incoming audio voltages (0 to 15)
    int audioA = map(audioMeanA, baseAudioVoltage, topAudioVoltage, 0, 15);
    int audioB = map(audioMeanB, baseAudioVoltage, topAudioVoltage, 0, 15);
    int audioC = map(audioMeanC, baseAudioVoltage, topAudioVoltage, 0, 15);
    // keep track of previos values used including the slide down
    volumeChannelA_Prev = volumeChannelA;
    volumeChannelB_Prev = volumeChannelB;
    volumeChannelC_Prev = volumeChannelC;

    // Note: volumeChannelX will never go negative as audioX can't
    if (audioA >= volumeChannelA_Prev)
      volumeChannelA = audioA;  // Fresh audio detected, refresh UV meter
    else
      volumeChannelA--;         // slide down over time

    if (audioB >= volumeChannelB_Prev)
      volumeChannelB = audioB;
    else
      volumeChannelB--;

    if (audioC >= volumeChannelC_Prev)
      volumeChannelC = audioC;
    else
      volumeChannelC--;


    oled.setCursor((128 / 2) - 6 - 6 - 6, DISPLAY_ROW_VU_METER_TOP);
    displayVuMeterTopPart(volumeChannelA);
    displayVuMeterTopPart(volumeChannelB);
    displayVuMeterTopPart(volumeChannelC);
    oled.setCursor((128 / 2) - 6 - 6 - 6, DISPLAY_ROW_VU_METER_BOTTOM);
    displayVuMeterBottomPart(volumeChannelA);
    displayVuMeterBottomPart(volumeChannelB);
    displayVuMeterBottomPart(volumeChannelC);

    oled.setCursor(128 - 32, DISPLAY_ROW_BYTES_LEFT);
    oled.print(fileSize / 1024);
    oled.print("K ");

    bitClear(playFlag, FLAG_REFRESH_DISPLAY);
    count -= (256 / 32); // letting byte wrap

    //-----------

    // New prototype UV code with two sets of x8 LEDS using AY I/O ports via a ULN2803 tranistor array
    // WORKS!!!  For now just populating one side of PCB, other side is same but via port B

    setAYMode(INACTIVE);
    setAYMode(LATCH_ADDRESS);
    PORTD = PSG_REG_ENABLE;
    setAYMode(INACTIVE);
    //setAYMode(INACTIVE);
    PORTD = B01000000 | LastAYEnableRegisiterUsed;   //OIA - PORT A
    setAYMode(WRITE);
    //setAYMode(INACTIVE);
    setAYMode(INACTIVE);
    setAYMode(LATCH_ADDRESS);
    PORTD = PSG_REG_IOA;
    setAYMode(INACTIVE);

    
    //  Set bits based on the volume level (0 to 15)
    const byte threshold = 2; // Pre-calculate 15 / (8 - 1)
    byte bits = 0;
    for (int i = 0; i < 8; i++) {
      if (audioA > i * threshold) {
        bits |= 1 << (7 - i);
      }
    }

    PORTD = bits; //byte;
    setAYMode(WRITE);
    setAYMode(INACTIVE);
    
    //-----------
  }
  cacheSingleByteRead();  //cache more music data if needed
}

// Generate two bus control signals for AY/PSG pins (BDIR and BC1)
// ------+-----+---------------
//  BDIR | BC1 |  PSG FUNCTION
// ------+-----+---------------
//    0  |  0  |  INACTIVE
//    0  |  1  |  READ FROM PSG
//    1  |  0  |  WRITE TO PSG
//    1  |  1  |  LATCH ADDRESS
// ------+---------------------
void setAYMode(AYMode mode) {

  switch (mode) {
    case INACTIVE:
      PORTB &= ~(1 << 0);  // BC1_pin   - order is important ?!?!
      PORTC &= ~(1 << 2);  // BDIR_pin
      //digitalWrite(BDIR_pin, LOW);
      //digitalWrite(BC1_pin, LOW);
      break;
    case READ:
      PORTB |= (1 << 0);
      PORTC &= ~(1 << 2);
      //digitalWrite(BDIR_pin, LOW);
      //digitalWrite(BC1_pin, HIGH);
      break;
    case WRITE:
      PORTB &= ~(1 << 0);
      PORTC |= (1 << 2);
      //digitalWrite(BDIR_pin, HIGH);
      //digitalWrite(BC1_pin, LOW);
      break;
    case LATCH_ADDRESS:
      PORTB |= (1 << 0);
      PORTC |= (1 << 2);
      //digitalWrite(BDIR_pin, HIGH);
      //digitalWrite(BC1_pin, HIGH);
      break;
  }

  //https://garretlab.web.fc2.com/en/arduino/inside/hardware/arduino/avr/cores/arduino/Arduino.h/digitalPinToBitMask.html
}



// NOTE: *** Used by interrupt, keep code lightweight ***
void writeAY(byte psg_register, byte data) {
  if (psg_register < PSG_REG_TOTAL) {
    //
    // ADDRESS PSG REGISTER SEQUENCE (LATCH_ADDRESS)
    // The “Latch Address.“. sequence is normally an integral part of the
    // write or read sequences, but for simplicity is illustrated here as an
    // individual sequence. Depending on the processor used the program
    // sequence will normally require four principal microstates:
    // (1) send NACT (inactive); (2) send INTAK (latch address);
    // (3) put address on bus: (4) send NACT (inactive).
    // [Note: within the timing constraints detailed in Section 7, steps (2) and (3) may be interchanged.]

    setAYMode(INACTIVE);  // not really needed due to call order after setup
    setAYMode(LATCH_ADDRESS);
    PORTD = psg_register;
    setAYMode(INACTIVE);

    // WRITE DATA TO PSG SEQUENCE (WRITE)
    // The “Write to PSG” sequence, which would normally follow immediately after an address sequence, requires four principal microstates:
    // (1) send NACT (inactive); (2) put data on bus;
    // (3) send DWS (write to PSG); (4) send NACT (inactive)
    PORTD = data;
    setAYMode(WRITE);
    setAYMode(INACTIVE);

    //  switch (port) {
    //  case PSG_REG_ENV_SHAPE: if (ctrl == 255) return; // Envelope bugfix ???? NOT TESTED
    // }
  }
}

inline bool readBuffer(byte& dat) {
  if (isCacheReady()) {
    dat = playBuf[circularBufferReadIndex];
    ADVANCE_PLAY_BUFFER
    return true;
  }
  else {
    return false;
  }
}

// Q: Why top and bottom functions?
// A: Two characters are joined to make a tall VU meter.
inline void displayVuMeterTopPart(byte volume) {
  if (volume > 15)
    volume = 15;

  if (volume >= 8) {
    // Note: x8 characters have been redefined for the VU memter starting from '!'
    oled.print((char)('!' + (((volume)&0x07))));
    oled.print((char)('!' + (((volume)&0x07))));
  } else {
    oled.print(F("  "));  // nothing to show, clear.  (F() puts text into program mem)
  }
}

inline void displayVuMeterBottomPart(byte volume) {
  if (volume > 15)
    volume = 15;

  if (volume < 8) {
    // Note: x8 characters have been redefined for the VU memter starting from '!'
    oled.print((char)('!' + (((volume)&0x07))));
    oled.print((char)('!' + (((volume)&0x07))));
  }
    else {
      oled.print(F("(("));  // '(' is redefined as a solid bar for VU meter
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
  oled.print(F("PSG Music Player\nfor the AY-3-8910\n\nver"));
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
  PORTD = 0;

  // pins for sound sampling to drive the VU Meter
  pinMode(A6, INPUT); // sound in
  pinMode(A1, INPUT); // sound in
  pinMode(A0, INPUT); // sound in
  // Assign pins for the AY Chip
  pinMode(ResetAY_pin, OUTPUT);
  pinMode(BC1_pin, OUTPUT);
  pinMode(BDIR_pin, OUTPUT);
  pinMode(AY_Clock_pin, OUTPUT);  // ay clock
  ///////////////////////////pinMode(A2, OUTPUT);
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

// internal counter to slow things down (
volatile static byte ScaleCounter = 0;

ISR(TIMER2_COMPA_vect) {

  // Sample AY channels A,B and C
  audioAsum += analogRead(AUDIO_FEEDBACK_B);
  audioBsum += analogRead(AUDIO_FEEDBACK_B);
  audioCsum += analogRead(AUDIO_FEEDBACK_C);

  // 50 Hz, 250 interrupts per second / 50 = 5 steps per 20ms
  if (++ScaleCounter >= (DUTY_CYCLE_FOR_TIMER2 / 50) ) {

    audioMeanA = audioAsum / (DUTY_CYCLE_FOR_TIMER2 / 50);
    audioMeanB = audioBsum / (DUTY_CYCLE_FOR_TIMER2 / 50);
    audioMeanC = audioCsum / (DUTY_CYCLE_FOR_TIMER2 / 50);
    audioAsum = 0;
    audioBsum = 0;
    audioCsum = 0;

    if (interruptCountSkip > 0) {
      //     oled.setCursor(80, 0);
      //      oled.print(ddd++);
      interruptCountSkip--;
    } else {
      processPSG();
    }
    ScaleCounter = 0;

    bitSet(playFlag, FLAG_REFRESH_DISPLAY);
  }
}


// Arduino Nano Timer notes:
// Each Timer/Counter has two output compare pins.
//  - Timer/Counter 0 OC0A and OC0B pins are called PWM pins 6 and 5 respectively.
//  - Timer/Counter 1 OC1A and OC1B pins are called PWM pins 9 and 10 respectively.
//  - Timer/counter 2 OC2A and OC2B pins are called PWM pins 11 and 3 respectively.
// Arduino libs provide 'F_CPU' as the clock frequency of the Arduino board you are compiling for.  
#define MUSIC_FREQ 2000000
// Configure Timer 1 to generate a square wave
void setupClockForAYChip() {
  TCCR1A = bit(COM1A0) ;// Set Compare Output Mode to toggle pin on compare match
  TCCR1B = bit(WGM12) | bit(CS10); // Set Waveform Generation Mode to Fast PWM and set the prescaler to 1
  // Calculate the value for the Output Compare Register A
  OCR1A = ((F_CPU  / MUSIC_FREQ) / 2)  - 1;   // works out to be 3 for 16mhz arduino
  pinMode(AY_Clock_pin, OUTPUT); // Pin to output the clock signal
}

// can we remove 2 if no prescaler ????

#define INTERRUPT_FREQUENCY 50 

// Setup 8-bit timer2 to trigger interrupt (see ISR function)
void setupProcessLogicTimer() {
  cli();                            // Disable interrupts
  TCCR2A = _BV(WGM21);              // CTC mode (Clear Timer and Compare)
  // 16000000Hz (ATmega16MHz) / 256 prescaler = 62500Hz (duty cycle)
  TCCR2B =  _BV(CS22) | _BV(CS21);  // 256 prescaler  (CS22=1,CS21=1,CS20=0)
  OCR2A = DUTY_CYCLE_FOR_TIMER2;          // Set the compare value to control duty cycle
  TIFR2 |= _BV(OCF2A);              // Clear pending interrupts
  TIMSK2 = _BV(OCIE2A);             // Enable Timer 2 Output Compare Match Interrupt
  TCNT2 = 0;                        // Timer counter 2
  sei();                            // enable interrupts
  // https://onlinedocs.microchip.com/pr/GUID-93DE33AC-A8E1-4DD9-BDA3-C76C7CB80969-en-US-2/index.html?GUID-669CCBF6-D4FD-4E1D-AF92-62E9914559AA
}




// *** PSG HEADER DETAILS ***
//-----------------------------------------------------------------
// Offset   Bytes Used   Description
//-----------------------------------------------------------------
// [0]      3            Identifier test = "PSG"
// [3]      1            "end-of-text" marker (1Ah)
// [4]      1            Version number
// [5]      1            Playback frequency (for versions 10+)
// [6]      10           Unknown

// Example of PSG File Header(16 bytes), header is followed by the start of the raw byte data
// HEADER: 50 53 47 1A 00 00 00 00 00 00 00 00 00 00 00 00
// DATA  : FF FF 00 F9 06 16 07 38 FF 00 69 06 17 FF 00 F9 ...
//
// *** PSG PAYLOAD DETAILS ***
// PSG commands - music format for byte data from file (payload/body/data)
// - END_OF_INTERRUPT_0xFF
//  [0xff]              : End of interrupt (EOI) - waits for 20 ms
// - END_OF_INTERRUPT_MULTIPLE_0xFE
//  [0xfe],[byte]       : Multiple EOI, following byte provides how many times to wait 80ms (value of "1" is x4 longer that FF).
// - END_OF_INTERRUPT_MULTIPLE_0xFE
//  [0xfd]              : End Of Music
// - END_OF_MUSIC_0xFD
//  [0x00..0x0f],[byte] : PSG register, following byte is accompanying data for this register
//
// note: register numbers ranging from 16 to 252 (0x10 to 0xfc) don't exist for the AY3-891x chips and are ignored

void processPSG() {
  // Called by interrupt so keep this method as lightweight as possiblle
  byte action, dat;
  while (readBuffer(action)) {
    switch (action) {
      case END_OF_MUSIC_0xFD: bitSet(playFlag, FLAG_NEXT_TUNE);  return;
      case END_OF_INTERRUPT_0xFF: return;
    }
    if (readBuffer(dat)) {
      switch (action) {
        case PSG_REG_ENABLE:
        //case PSG_REG_IOA:
        //case PSG_REG_IOB:
          writeAY(action, dat | B11000000);  // enable sound bits - Forcing I/O portA, portB (B11000000) to be enabled
          LastAYEnableRegisiterUsed = dat;   // keep last used enabled bits so we, enable I/O later without losing sound bits
        break;
        case END_OF_INTERRUPT_MULTIPLE_0xFE:
          if ((dat == 0xff) && (fileSize / 32 == 0)) {
            // Some tunes have very long pauses at the end (caused by repeated sequences of "0xfe 0xff").
            // For example "NewZealandStoryThe.psg" has a very long pause at the end, I'm guessing by design to handover to the ingame tune.
            interruptCountSkip = 4; // 4 works ok on trouble tunes as a replacement pause
          }
          else {
            interruptCountSkip = dat << 2; //   x4, to make each a 80 ms wait - part of formats standard
          }
          return; // do nothing in this cycle
        default:  // 0x00 to 0xFC
          writeAY(action, dat); // port & control regisiter
          break;  // read more data - while loop
      }
    } else {
      circularBufferReadIndex--;
    }
  }
}

inline int advancePastHeader() {
  file.seek(16); // absolute position -  Skip header
  return 16;
}

// Here we are checking for the "PSG" file's header, byte by byte to help limit memory usage.
// note: if found, global file will have advanced 3 bytes
bool isFilePSG() {
  if (file) {
    if (!file.isDirectory()) {
      // Doing every little bit to save some dynamic memory.
      // Could put "PSG" in program mem, but that would still require 
      // a counter eating away at the stack (yes one byte but I'm very low on stack/memory)
      return (file.available() && file.read() == 'P' && file.available() &&  file.read() == 'S' && file.available() && file.read() == 'G');
    }
  }
  return false;
}

int countPlayableFiles() {
  int count = 0;
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
    int k=0;
    file = root.openNextFile();
    while (file) {
      if (isFilePSG()) {
        if (k == fileIndex) {
          fileSize = file.size();
          if (fileSize > 16) { // check we have a body, 16 PSG header
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

  if (fileSize >= 1) {
    playBuf[circularBufferLoadIndex] = file.read();
    fileSize--;
  }
  else {
    playBuf[circularBufferLoadIndex] = END_OF_MUSIC_0xFD;
  }
  ADVANCE_LOAD_BUFFER
}

// Reset AY chip to stop sound output
void resetAY() {
  setAYMode(INACTIVE);
  // Reset line needs to go High->Low->High for AY38910/12
  digitalWrite(ResetAY_pin, HIGH);  // just incase start high
  delay(1);
  digitalWrite(ResetAY_pin, LOW); // Reset pulse width must be min of 500ns
  delay(1);
  digitalWrite(ResetAY_pin, HIGH);
  setAYMode(INACTIVE);


  // TEST CODE FOR SETTING  IO ON AY
  // 0=INPUT , 1=OUTPUT  (OIA=Bit6, OIB=Bit7)
// soon to become UV METER !!!!!!!!!!!W

/*
    setAYMode(INACTIVE);
    setAYMode(LATCH_ADDRESS);
    PORTD = PSG_REG_ENABLE;
    setAYMode(INACTIVE);

    setAYMode(INACTIVE);
    PORTD = B01000000;   //OIA
    setAYMode(WRITE);
    setAYMode(INACTIVE);

    setAYMode(INACTIVE);
    setAYMode(LATCH_ADDRESS);
    PORTD = PSG_REG_IOA;
    setAYMode(INACTIVE);

    setAYMode(INACTIVE);
    PORTD = B10101010;
    setAYMode(WRITE);
    setAYMode(INACTIVE);
*/
    // TEST TO SEE IF THE ABOVE CAN WORK OVER TIME ?  DOES THE IO KEEP VALUES ...IT SHOULD?  
    // ABOVE STATE SHOULD REMAIN WHILE MUSIC IS PLAYING.. I.E.  WHILE OTHER REGISTERS ARE BEING SET

    // YEP THIS TEST CODE WORKS :)     CAN READ PINS WITH 0v,+5v WITH M/METER

}





void loop_TEST() {

  // Test the file handling
  // make sure everthing works and closes files and stuff
  //
  // leave running - if it does not lock up then the logic is "probably" ok.
  //
  for (;;) {

    selectFile(fileIndex);
    if (++fileIndex >=  filesCount) {
      fileIndex = 0;
    }
    oled.setCursor(0, DISPLAY_ROW_FILENAME);
    oled.print((char*)file.name());
  }
}



// PORTB  (digital pin 8 to 13)
// PORTC  (analog input pins)
// PORTD  (digital pins 0 to 7)




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


/*
3.6 Registers R16 and R17 function as intermediate data storage regisI/O Port Data ters between the PSG/CPU data bus (DA0--DA7) and the two I/O
ports (IOA7-IOA0 and IOB7--1OB0). Both ports are available in the
Store AY-3-8910; only I/O Port A is available in the AY-3-8912. Using
registers R16 and R17 for the transfer of I/O data has no effect at all
(Registers R16, R17) on sound generation.
To output data from the CPU bus to a peripheral device connected to I/O Port A would require only the following steps:
1. Latch address R7 (select Enable register)
2. Write data to PSG (setting B6 of R7 to “1”)
3. Latch address R16 (select IOA register)
4. Write data to PSG (data to be output on I/O Port A)

To input data from I/O Port A to the CPU bus would require the following:
1. Latch address R7 (select Enable register)
2. Write data to PSG (setting B6 to R7 to “0”)
3. Latch address R16 (select IOA register)
4. Read data from PSG (data from I/O Port A)
Note that once loaded with data in the output mode, the data will
remain on the I/O port(s) until changed either by loading different
data, by applying a reset (grounding the Reset pin), or by switching to
the input mode.
Note also that when in the input mode, the contents of registers R16
and/or R17 will follow thesignals applied to the I/O port(s). However,
transfer of this data to the CPU bus requires a “read” operation as
described above.

*/