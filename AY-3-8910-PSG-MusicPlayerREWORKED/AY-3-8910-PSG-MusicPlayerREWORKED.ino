// This sketch plays PSG files via a AY-3-8910 which is a 3-voice programmable sound generator (PSG)
// Code has been optimised (mainly to save space) and will also fit onto a 'ATmega168'

//TODO .. add more user inputs/buttons, play, next, back, random tune ...
//TODO .. [DONE] send audio into Arduino / use analogRead /  reading for a VU meter
//TODO .. [DONE] maybe use IO on AY chip as a VU Meter
//TODO ...[DONE] SD CARD WIH NO MUSIC
//TODO ... [DONE] REWORK HARDWARE - lose chip for Serial to Parallel Shifting
//TODO ... [DONE] lose fat.h and use SD.h - changeover was easier than I was expecting


// NOTE: platform.txt (C:\Users\Admin\AppData\Local\Arduino15\packages\arduino\hardware\avr\1.8.6)
// Update can sometimes change the compiler settings to '-Ofast'
// This sketch is using Something like #optimise='-Os' or '-O2' to allow code to fit into a ATmega168 pro mini

//https://www.pcbway.com/project/shareproject/AY_3_8910_Sound_Generator_Arduino_Nano_Controlled_54bbb339.html
//https://github.com/Andy4495/AY3891x
// tips on converting AY to flat files


// "logic green nano"  compile using these settings:  board='LGT8F328' | clock=16Mz | variant=328P-LQFP48 mini-EVB
// Only have Arduino IDE 2.0 with this installed


// Arduino (Nano) Pins used: See "pins.h"
//
// *** Arduino pins by port layout ***
// PORTB  (digital pin 8 to 13)
// PORTC  (analog input pins)
// PORTD  (digital pins 0 to 7)


#include <SPI.h>
#include <SD.h>                  // with this lib, works on LGT8F328P ... needed to stop using fat.h and use SD.h
#include "SSD1306AsciiAvrI2c.h"  // OLED display  ... USE: #define INCLUDE_SCROLLING 0
#include "fudgefont.h"           // Based on the Adafruit5x7 font, with '!' to '(' changed to work as a VU BAR (8 chars)
#include "pins.h"
#include "AY3891xRegisters.h"
/*
extern void setupPins();
extern void resetAY();
extern void setupClockForAYChip();
extern void setupOled();
extern int16_t countPSGFiles();
extern void setupProcessLogicTimer();
extern void cacheSingleByteRead();
extern void displayVuMeterTopPart(byte volume);
extern void displayVuMeterBottomPart(int volume);
extern bool isCacheReady();
extern void setAYMode(AYMode mode);
extern void setChannelVolumes(byte a, byte b, byte c);
extern void SendVUMeterDataToAY_IO(byte audioA, byte audioB, byte audioC);
extern bool isPsgFile(const char* fileName);
extern void writeUint32(File& file, uint32_t value);
extern void writeUint16(File& file, uint16_t value);
extern bool endsWithIgnoreCase(const char* str, const char* suffix);
extern const char* getFileNameByIndex(File& tempFile, size_t index);
extern size_t CreatePsgFileList(File& outputFile);
*/
#define VERSION ("2.4")

// Choose the buffer size based on available RAM
// ATmega328 (2K RAM): uses larger 256-byte buffer for optimal performance
#define BUFFER_SIZE (256)
// ATmega168 (1K RAM): uses smaller 64-byte buffer to fit within memory
//#define BUFFER_SIZE (64)        DOES NOT WORK ON 1K NOW , MAYBE POSSIBLE

byte playBuf[BUFFER_SIZE];  // Playback buffer, sized based on the selected buffer size option

// Control for circular buffer indexing, wraps to start when reaching buffer end
#if (BUFFER_SIZE == 256)
#define ADVANCE_PLAY_BUFFER circularBufferReadIndex++;
#define ADVANCE_LOAD_BUFFER circularBufferLoadIndex++;
#else
#define ADVANCE_PLAY_BUFFER \
  circularBufferReadIndex++; \
  if (circularBufferReadIndex >= BUFFER_SIZE) { circularBufferReadIndex = 0; }
#define ADVANCE_LOAD_BUFFER \
  circularBufferLoadIndex++; \
  if (circularBufferLoadIndex >= BUFFER_SIZE) { circularBufferLoadIndex = 0; }
#endif

#define RESET_BUFFERS circularBufferLoadIndex = circularBufferReadIndex = 0;

// PSG command definitions for audio data handling
#define END_OF_INTERRUPT_0xFF (0xff)
#define END_OF_INTERRUPT_MULTIPLE_0xFE (0xfe)
#define END_OF_MUSIC_0xFD (0xfd)

// We have three Arduino Timers to pick from:-
//  Timer0: 8 bits; Used by libs, e.g millis(), micros(), delay()   - LEAVING ALONE
//  Timer1: 16 bits; Use by Servo, VirtualWire and TimerOne library - USED FOR AY CLOCK SIGNAL
//  Timer2: 8 bits; Used by the tone() function                     - USING FOR ISR logic interrupt

// Arduino Timer2  (8-bit timer)
// Timer configuration to achieve 50Hz PWM, triggering every 20ms
const byte INTERRUPT_FREQUENCY = 50;
const byte DUTY_CYCLE_FOR_TIMER2 = 250;  // note: timer 2 has a 8 bit resolution
//
// History: for calculating the prescaler and duty
//
// Searching for any of these to be a multiple of 50HZ
// 1024: (16000000Hz / 1024 prescale) = 15625Hz
//   gives factors 1, 5, 25, 125 ... NO - nothing here fits my 50HZ target i.e. (125/50 = 2.5)
// 256:  (16000000Hz / 256 prescale)  = 62500Hz
//   gives factors 1, 2, 4, 5, 10, 20, 25, 50, 100, 125, 250                        >>>> 250 WINNER <<<<<
// 128:  (16000000Hz / 128 prescale)  = 125000Hz
//   gives factors 1, 2, 4, 5, 8, 10, 20, 25, 40, 50, 100, 125, 200, 250 ... NOPE To fast
//  64:  (16000000Hz / 64 prescale)   = 250000Hz
//   gives factors 1, 2, 4, 5, 8, 10, 16, 20, 25, 40, 50, 80, 100, 125, 200, 250 - NOPE, yeah way to fast
//
// 16000000Hz / 256  = 62500Hz
// 62500Hz / duty 250 = 250 interrupts per second (so will scale by 5 in ISR code)
//
#define I2C_ADDRESS 0x3C  // 0x3C or 0x3D
SSD1306AsciiAvrI2c oled;

// Display row definitions for display data
#define DISPLAY_ROW_FILENAME (0)
#define DISPLAY_ROW_FILE_COUNTER (1)
#define DISPLAY_ROW_BYTES_LEFT (1)
#define DISPLAY_ROW_VU_METER_TOP (2)
#define DISPLAY_ROW_VU_METER_BOTTOM (3)

// Play flags for control states (e.g., start, pause, refresh)
enum { 
       FLAG_NEXT_TUNE,
       FLAG_BACK_TUNE,
       FLAG_PAUSE_TUNE,
       FLAG_INIT_SELECTED_TUNE,
       FLAG_REFRESH_DISPLAY,
       FLAG_PLAYING,
       //FLAG_BUTTON_BACK,
       //FLAG_BUTTON_FORWARD,
      // FLAG_BUTTON_PLAY,
     //  FLAG_BUTTON_PAUSE,
       FLAG_UPDATE_INFO,
       FLAG_PLAY_OR_PAUSE_HANDLED,

       FLAG_NEXT_TUNE_HANDLED,
       FLAG_PSG_FILE_ERROR,
       FLAG_DISPLAY_TIMER_ERROR
       
};

volatile int16_t playFlag;            // things like 50hz refresh from ISR
volatile int interruptCountSkip = 0;  // Don't play new sequences via interrupt when this is positive (do nothing for 20ms x interruptCountSkip)
uint32_t fileRemainingBytesToRead = 0;

// Circular buffer indices
byte circularBufferLoadIndex;  // Byte Optimied : this counter wraps back to zero by design (using 256 buffer option)
byte circularBufferReadIndex;  // Byte Optimied : this counter wraps back to zero by design

static byte LastAYEnableRegisiterUsed = 0;  // lets us set I/O ports without stamping over the AY's already enabled sound bits

// Audio
uint16_t baseAudioVoltage = 0;     // initialised once in setup for VU meter  - lowest point possible
uint16_t topAudioVoltage = 0;      //                 for VU meter scale top - highest point possible
volatile uint16_t audioMeanA = 0;  //
volatile uint16_t audioMeanC = 0;  //   (Updated each display refresh)
volatile uint16_t audioMeanB = 0;  //

uint16_t lastCurrentIndex = 0;
uint16_t currentIndex = 0;  // Global or static variable to track the current file index
uint16_t totalFiles;        // Set this from the temp.lst header
const char* tempFileName = "/temp.lst";

// SD card root directory and file object
File root;  // Note: 'File' struct "EATS UP 27 BYTES"
File file;

const uint8_t maxHoldDelay = 120;   // Minimum delay at max scroll acceleration
const float delayReductionFactor = 0.80;  // Factor for reducing delay
const uint16_t initHoldDelay = 800;  // Initial delay before acceleration
uint16_t holdDelay = initHoldDelay;
unsigned long lastPressTime = 0;  // Tracks last press time for any button

uint8_t lastPSGRegAmplitudeA=0;
uint8_t lastPSGRegAmplitudeB=0;
uint8_t lastPSGRegAmplitudeC=0;

// Startup the display, audit files, fire-up timer for AY at 1.75Mhz(update: re-assigned pins so now 2Mhz, OC1A is not as flexable as OCR2A) , reset AY chip
void setup() {

  //Serial.begin(9600);   // this library eats 177 bytes, dont forget to remove from release!!!
  //Serial.println(sizeof(SdFat));
  // NOTE: NOW USING TX, RX LINES .. using serial debug will mess things up
  // So to use debug first comment out PORTD writes, found in places like writeAY()

  setupOled();
  setupPins();
  setupClockForAYChip();  // Start AY clock before using the AY chip.
  resetAY();

SD_CARD_MISSING_RETRY:
  delay(1200);  // allow time to read Version or Error Message

  if (SD.begin(CS_SDCARD_pin)) {

    //------------------------------------
    if (SD.exists(tempFileName)) {
      SD.remove(tempFileName);
    }
    root = SD.open("/");

    if (!root) {
      oled.println(F("SD card failed"));  // TODO ... optimise common parts of strings!!!
      SD.end();
      goto SD_CARD_MISSING_RETRY;
    }

    file = SD.open(tempFileName, FILE_WRITE);
    totalFiles = CreatePsgFileList(file);
    file.close();

  } else {
    oled.println(F("Waiting for SD card"));
    goto SD_CARD_MISSING_RETRY;
  }

  //  filesCount = countPSGFiles();
  if (totalFiles == 0) {
    oled.println(F("SD card empty"));
    goto SD_CARD_MISSING_RETRY;
  }

  // start the logic interrupt up last
  setupProcessLogicTimer();

  // Sample AY audio lines (x3) for a baseline starting signal
  baseAudioVoltage = min(min(analogRead(AUDIO_FEEDBACK_A), analogRead(AUDIO_FEEDBACK_B)), analogRead(AUDIO_FEEDBACK_C));
  root.close();
  root = SD.open(tempFileName, FILE_READ);  // from now on reusing root for reading tempFile
  oled.clear();

  // -----------------------------------------------
  // Play First Tune Found/uses inital currentIndex
  bitSet(playFlag, FLAG_INIT_SELECTED_TUNE);
  bitSet(playFlag, FLAG_UPDATE_INFO);
  //------------------------------------------------
}

//Sketch uses 23756 bytes (77%) of program storage space. Maximum is 30720 bytes. (using String)
//Global variables use 1269 bytes (61%) of dynamic memory, leaving 779 bytes for local variables. Maximum is 2048 bytes.
//Sketch uses 23060 bytes (75%) of program storage space. Maximum is 30720 bytes. (using char* only)
//Global variables use 1269 bytes (61%) of dynamic memory, leaving 779 bytes for local variables. Maximum is 2048 bytes.
//Sketch uses 23112 bytes (75%) of program storage space. Maximum is 30720 bytes.  // reducing efforts
//Global variables use 1268 bytes (61%) of dynamic memory, leaving 780 bytes for local variables. Maximum is 2048 bytes.
//Sketch uses 23102 bytes (75%) of program storage space. Maximum is 30720 bytes.  // better reusing root!
//Global variables use 1241 bytes (60%) of dynamic memory, leaving 807 bytes for local variables. Maximum is 2048 bytes.


// Button Voltage Thresholds - Values: 0 to 1023 (0 to +5v)
const int NO_BUTTON_THRESHOLD = 940;
const int BUTTON_BACK_THRESHOLD = 845 - 45;  //780; 
const int BUTTON_FORWARD_MIN = 515-45;   ///480;
const int BUTTON_FORWARD_MAX = 512+45;   //580;
const int BUTTON_PLAY_PAUSE_MIN = 328-45;  //280;
const int BUTTON_PLAY_PAUSE_MAX = 328+45; //380;
// last button 20

void loop() {
  if (bitRead(playFlag, FLAG_REFRESH_DISPLAY)) {  // 50hz
    bitClear(playFlag, FLAG_REFRESH_DISPLAY);
    // Values read are 0 to 1023 (0 to +5v) - note: H/W uses voltage ladder.
    int buttonValue = analogRead(NextButton_pin);  // we will read the buttons here, 50hz is plenty.
    // Work out which button is pressed.
    // note: We are going for tapping or gradual acceleration when holding forward/back buttons.
    if (buttonValue < NO_BUTTON_THRESHOLD) {
      if (buttonValue > BUTTON_BACK_THRESHOLD) {
        handleButtonWithDelay(-1);
      } else if (buttonValue > BUTTON_FORWARD_MIN && buttonValue < BUTTON_FORWARD_MAX) {
        handleButtonWithDelay(1);
      } else if (buttonValue > BUTTON_PLAY_PAUSE_MIN && buttonValue < BUTTON_PLAY_PAUSE_MAX) {      
        playButtonlogic();
      } else {
        // TO DO - settings button here
      }
    } else {
      bitClear(playFlag, FLAG_PLAY_OR_PAUSE_HANDLED); // reports buttons are unpressed
      holdDelay = 0;  // Button released so we can reset button delay
    }
 
    updateDisplay();
  }
  handlePlayback();
  // Monitor for tune end (trigged on 'END_OF_MUSIC_0xFD' in processPSG)
  if (bitRead(playFlag, FLAG_NEXT_TUNE)) {
    bitClear(playFlag, FLAG_NEXT_TUNE);
    ChangeTune(1);
    bitSet(playFlag, FLAG_UPDATE_INFO);  // We made a change, lets display the new file information
    bitSet(playFlag, FLAG_INIT_SELECTED_TUNE);
  }
}

void playButtonlogic() {

  if (bitRead(playFlag, FLAG_PLAYING)) {
    if (bitRead(playFlag, FLAG_NEXT_TUNE_HANDLED)) { 
      // Start playing the selected tune
      bitClear(playFlag, FLAG_NEXT_TUNE_HANDLED);
      bitSet(playFlag, FLAG_INIT_SELECTED_TUNE);      // init new tune
      bitSet(playFlag, FLAG_PLAY_OR_PAUSE_HANDLED);   // Prevent rapid toggling between play and pause
      return;
    }
    if (!bitRead(playFlag, FLAG_PLAY_OR_PAUSE_HANDLED)) {
      // Pause playback
      bitClear(playFlag, FLAG_PLAYING); 
      bitSet(playFlag, FLAG_PAUSE_TUNE);
      bitSet(playFlag, FLAG_PLAY_OR_PAUSE_HANDLED);   // Prevent rapid toggling between play and pause
      setChannelVolumes(0, 0, 0);                     // Mute sound
      return;
    }
  }

  if (!bitRead(playFlag, FLAG_PLAY_OR_PAUSE_HANDLED)) {
    if (bitRead(playFlag, FLAG_PAUSE_TUNE)) {
      // resume playing
      bitClear(playFlag, FLAG_PAUSE_TUNE);
      bitSet(playFlag, FLAG_PLAYING);                 
      bitSet(playFlag, FLAG_PLAY_OR_PAUSE_HANDLED);  // Prevent rapid toggling between play and pause
      setChannelVolumes(lastPSGRegAmplitudeA, lastPSGRegAmplitudeB, lastPSGRegAmplitudeC);  // Restore volume
      return;
    }
  }

  if (!bitRead(playFlag, FLAG_PLAYING) && !bitRead(playFlag, FLAG_PAUSE_TUNE)) {
     // In a idle state, start playing the selected tune
    bitSet(playFlag, FLAG_INIT_SELECTED_TUNE);
    return;
 }
}


// Utility: Handles forward/backward button logic
void handleButtonWithDelay(int delta) {
//  if (bitRead(playFlag, flag)) {
 //   bitClear(playFlag, flag);
    if (millis() - lastPressTime > holdDelay) {
      ChangeTune(delta);
      bitSet(playFlag, FLAG_UPDATE_INFO);
      lastPressTime = millis();
      if (holdDelay <= 0) { holdDelay = initHoldDelay; }
      holdDelay = max((unsigned long)(holdDelay * delayReductionFactor), maxHoldDelay);
      bitSet(playFlag, FLAG_NEXT_TUNE_HANDLED);
    }
//  }
}

void ChangeTune(int8_t direction) {
  lastCurrentIndex = currentIndex;
  currentIndex = (currentIndex + direction + totalFiles) % totalFiles;  
}

void handlePlayback() { // polled by main loop
  if (bitRead(playFlag, FLAG_INIT_SELECTED_TUNE)) {
    bitClear(playFlag, FLAG_INIT_SELECTED_TUNE);

    if (initTune(currentIndex)) {  
      // Selected tune initialised OK
      bitSet(playFlag, FLAG_PLAYING);
    }else { 
      // load failed - not a real psg file
      bitSet(playFlag, FLAG_PSG_FILE_ERROR); 
    }
  }
  if (bitRead(playFlag, FLAG_PLAYING)) {
    cacheSingleByteRead();
  }
}

void updateDisplay() {

  if (bitRead(playFlag, FLAG_PSG_FILE_ERROR)) {
    static unsigned long errorDisplayTime = 0;
    if (!bitRead(playFlag, FLAG_DISPLAY_TIMER_ERROR)) {
      oled.clear();
      oled.setCursor(0, 0);
      oled.print(F("NOT A VALID PSG FILE"));
      errorDisplayTime = millis();
      bitSet(playFlag, FLAG_DISPLAY_TIMER_ERROR);
    }
    if (millis() - errorDisplayTime > 1000) {
      oled.clear();
      playFlag =0 ;
      bitSet(playFlag, FLAG_UPDATE_INFO);
      bitClear(playFlag, FLAG_DISPLAY_TIMER_ERROR);
    }
  }

  if (bitRead(playFlag, FLAG_PLAYING)) { 
    // playing
    updateVUMeter();
    updateBytesLeftDisplay();
  } 
  if (bitRead(playFlag, FLAG_PAUSE_TUNE)) {
    // paused
    oled.setCursor(128 - 32, DISPLAY_ROW_BYTES_LEFT);
    oled.print(F(" || ")); // pause indicator
  }

  if (bitRead(playFlag, FLAG_UPDATE_INFO)) {
    bitClear(playFlag, FLAG_UPDATE_INFO);
    updateFileInfo( getFileNameByIndex(root, currentIndex) );
  }
}

void updateFileInfo( const char* fileName  ) {

  // Update filename part
  oled.setCursor(0, DISPLAY_ROW_FILENAME);
  const uint8_t len = strlen(fileName);
  memset((void*)(fileName + len), ' ', 12 - len);
  oled.print((char*)fileName);
  // Now update the file counter display
  oled.setCursor(0, DISPLAY_ROW_FILE_COUNTER);
  oled.print(currentIndex + 1);
  oled.print(F("/"));
  oled.print(totalFiles);
  oled.print(F(" "));
}

void updateVUMeter() {
  topAudioVoltage = max(max(topAudioVoltage, audioMeanA), max(audioMeanB, audioMeanC));
  if (topAudioVoltage > 0) {
    // Prevent sudden peaks from dominating - This adjustment slightly widens the dynamic range, 
    // so things are not overly restricted by a high peak that is rarely reached. 
    topAudioVoltage--;  
    if (topAudioVoltage < baseAudioVoltage) {
      topAudioVoltage = baseAudioVoltage;
    }
  }

  uint8_t volumeChannelA = map(audioMeanA, baseAudioVoltage, topAudioVoltage, 0, 15);
  uint8_t volumeChannelB = map(audioMeanB, baseAudioVoltage, topAudioVoltage, 0, 15);
  uint8_t volumeChannelC = map(audioMeanC, baseAudioVoltage, topAudioVoltage, 0, 15);

  oled.setCursor((128 / 2) - 18, DISPLAY_ROW_VU_METER_TOP);
  displayVuMeterTopPart(volumeChannelA);
  displayVuMeterTopPart(volumeChannelB);
  displayVuMeterTopPart(volumeChannelC);

  oled.setCursor((128 / 2) - 18, DISPLAY_ROW_VU_METER_BOTTOM);
  displayVuMeterBottomPart(volumeChannelA);
  displayVuMeterBottomPart(volumeChannelB);
  displayVuMeterBottomPart(volumeChannelC);

  SendVUMeterDataToAY_IO(volumeChannelA, volumeChannelB, volumeChannelC);

}

void updateBytesLeftDisplay() {
  oled.setCursor(128 - 32, DISPLAY_ROW_BYTES_LEFT);
  //oled.print(analogRead(NextButton_pin));
  oled.print(fileRemainingBytesToRead / 1024);
  oled.print(F("K "));
}

void mixVolumes(uint8_t A, uint8_t B, uint8_t C, uint8_t* output1, uint8_t* output2) {

  // Mix the volumes , incoming should be in range 0 to 15
  uint16_t mix1 = A + A + A + B;
  uint16_t mix2 = C + C + C + B;  //15*4

  // Initialize outputs
  *output1 = 0;
  *output2 = 0;

  // Set bits for output1 and output2
  for (int i = 0; i < 8; i++) {
    uint8_t threshold = ((i + 1) * 8) - 4;
    if (mix1 >= threshold) {
      *output1 |= (1 << i);
    }
    if (mix2 >= threshold) {
      *output2 |= (1 << i);
    }
  }
}

void SendVUMeterDataToAY_IO(uint8_t audioA, uint8_t audioB, uint8_t audioC) {
  //enable AY's I/O preserving anthing in use
  writeAY(PSG_REG_ENABLE, B11000000 | LastAYEnableRegisiterUsed);

  byte bitsA = 0, bitsB = 0;
  mixVolumes(audioA, audioB, audioC, &bitsA, &bitsB);

  writeAY(PSG_REG_IOA, bitsB);  // IOA goes to Right side of VU segment!
  writeAY(PSG_REG_IOB, bitsA);  // IOB goes to Left side of VU segment!
}

void setChannelVolumes(byte a, byte b, byte c) {
  writeAY(PSG_REG_AMPLITUDE_A, a);
  writeAY(PSG_REG_AMPLITUDE_B, b);
  writeAY(PSG_REG_AMPLITUDE_C, c);
}

// Set AY8910 hardware pins BDIR and BC1 (AY modes)
// BusDirection signal (BDIR) and two BusControl lines (BC1, BC2).
//----------------------------------------------
// BDIR BC2 BC1 State
//----------------------------------------------
//  0    1   0   Inactive
//  0    1   1   Read from external device
//  1    1   0   Write to external device
//  1    1   1   Latch Address
//----------------------------------------------
void setAYMode(AYMode mode) {
  switch (mode) {
    case INACTIVE:
      PORTB &= ~(1 << 0);  // feeds to the BC1_pin
      PORTC &= ~(1 << 2);  // feeds to the BDIR_pin
      break;
    case READ:
      PORTB |= (1 << 0);
      PORTC &= ~(1 << 2);
      break;
    case WRITE:
      PORTB &= ~(1 << 0);
      PORTC |= (1 << 2);
      break;
    case LATCH_ADDRESS:
      PORTB |= (1 << 0);
      PORTC |= (1 << 2);
      break;
  }
  //https://garretlab.web.fc2.com/en/arduino/inside/hardware/arduino/avr/cores/arduino/Arduino.h/digitalPinToBitMask.html
}

void writeAY(byte psg_register, byte data) {
  if (psg_register < PSG_REG_TOTAL) {

    // PSG REGISTER SEQUENCE; "Latch Address"  (write or read) sequence:-
    // (1) send NACT (inactive); (2) send INTAK (latch address);
    // (3) put address on bus: (4) send NACT (inactive).
    // [within carefull timing constraints, steps (2) and (3) may be interchanged]
    setAYMode(INACTIVE);  // not really needed due to call order after setup
    setAYMode(LATCH_ADDRESS);
    PORTD = psg_register;
    setAYMode(INACTIVE);

    // WRITE DATA TO PSG SEQUENCE (WRITE)
    // The “Write to PSG” sequence, normally follows immediately after an address sequence:-
    // (1) send NACT (inactive); (2) put data on bus;
    // (3) send DWS (write to PSG); (4) send NACT (inactive)
    PORTD = data;
    setAYMode(WRITE);
    setAYMode(INACTIVE);
  }
}

// Q: Why split into top and bottom functions?
// A: Each VU meter bar is composed of two parts (top and bottom) to create a taller display for each volume level.
//    The `displayVuMeterTopPart` function handles the top half of the VU bar, displaying only the upper segments
//    based on the volume level (0-15). If the volume is above halfway (8 or more), a custom character is chosen
//    from redefined character set, starting from '!', to indicate a filled upper section.
//    Otherwise, blank spaces are written to clear the top part when the volume is low.
inline void displayVuMeterTopPart(byte volume) {
  volume &= 0x0f;
  if (volume >= 8) {
    // Note: x8 characters have been redefined for the VU memter starting from '!'
    char c = '!' + (((volume)&0x07));
    oled.write(c);  // each is x2 chars wide (just looks better wider)
    oled.write(c);  // ""
  } else {
    oled.write(' ');  // Clear top part if volume is low
    oled.write(' ');  // ""
  }
}

inline void displayVuMeterBottomPart(byte volume) {
  volume &= 0x0f;
  if (volume < 8) {
    // Note: x8 characters have been redefined for the VU memter starting from '!'
    char c = '!' + (((volume)&0x07));
    oled.write(c);  // x2 chars wide
    oled.write(c);  // ""
  } else {
    oled.write('(');  // '(' is redefined as a solid bar for VU meter
    oled.write('(');  // ""
  }
}

void setupOled() {
  oled.begin(&Adafruit128x32, I2C_ADDRESS);
  delay(100);
  oled.begin(&Adafruit128x32, I2C_ADDRESS);  // some hardware is slow to initialise, first call does not work.
  oled.setFont(fudged_Adafruit5x7);          // original Adafruit5x7 font with tweeks at start for VU meter
  oled.clear();
  oled.print(F("PSG Music Player\nusing the AY-3-8910\n\nver"));
  oled.println(F(VERSION));
}

// Arduino (Atmega) pins default to INPUT (high-impedance state)
// Configure the direction of the digital pins. (pins default to INPUT at power up)
void setupPins() {

  DDRD = 0xff;  // all to OUTPUT
  PORTD = 0;    // clear data pins connected to AY

  // pins for sound sampling to drive the VU Meter
  pinMode(AUDIO_FEEDBACK_A, INPUT);  // sound in
  pinMode(AUDIO_FEEDBACK_B, INPUT);  // sound in
  pinMode(AUDIO_FEEDBACK_C, INPUT);  // sound in
  // Assign pins for the AY Chip
  pinMode(ResetAY_pin, OUTPUT);
  pinMode(BC1_pin, OUTPUT);
  pinMode(BDIR_pin, OUTPUT);
  pinMode(AY_Clock_pin, OUTPUT);
  // Assign pins for the user input
  pinMode(NextButton_pin, INPUT);
  // SD card
  pinMode(CS_SDCARD_pin, OUTPUT);
  pinMode(MOSI_SDCARD_pin, INPUT);
  pinMode(MISO_SDCARD_pin, INPUT);
  pinMode(SCK_SDCARD_pin, INPUT);
}

/*  
 * Interrupt Driven - keep this function lightweight  
 */
ISR(TIMER2_COMPA_vect) {

  // This interrupt logic runs at a higher frequency than needed. The best fit for the ISR setup was 250Hz,
  // due to the 8-bit resolution of Arduino's 'timer2'. Therefore, we need to ignore/scale this frequency
  // down to match the 50Hz PAL playback rate used by 99.9% of music.
  static byte ISR_Scaler = 0;  // used to slow frequency down to 50Hz

  static int audioAsum = 0;  // Sum accumulated at the faster ISR speed and averaged at display rate
  static int audioBsum = 0;  //   (reset to zero each display refresh)
  static int audioCsum = 0;  //

  // Sample AY channels A,B and C
  audioAsum += analogRead(AUDIO_FEEDBACK_A);  // TODO ... need a way to reset these to zero (not high priority thing, if even really needed)
  audioBsum += analogRead(AUDIO_FEEDBACK_B);
  audioCsum += analogRead(AUDIO_FEEDBACK_C);

  // Need 50 Hz playback, so we wait 250 interrupts per second. Giving 250/50 = 5 steps per 20ms
  if (++ISR_Scaler >= (DUTY_CYCLE_FOR_TIMER2 / INTERRUPT_FREQUENCY)) {

    // Why's ISR the code like this... Better to have the above onging sums as statics and NOT volatile.
    // Then these next three volatiles can be shared ouside the ISR, rather than force the ISR to deal with volatiles constanlty.
    // (if this part was moved out, then the ISR would suffer as the sums would need to be volatile)
    audioMeanA = audioAsum / (DUTY_CYCLE_FOR_TIMER2 / INTERRUPT_FREQUENCY);
    audioMeanB = audioBsum / (DUTY_CYCLE_FOR_TIMER2 / INTERRUPT_FREQUENCY);
    audioMeanC = audioCsum / (DUTY_CYCLE_FOR_TIMER2 / INTERRUPT_FREQUENCY);
    audioAsum = 0;
    audioBsum = 0;
    audioCsum = 0;

    if (interruptCountSkip > 0) {  // Don't play sequences when positive
      interruptCountSkip--;
    } else {
      if (bitRead(playFlag, FLAG_PLAYING)) {
        processPSG(); // from here gets called at the needed 50Hz rate
      }
    }
    ISR_Scaler = 0;

    // 50Hz rate - also a good timing refresh rate to be reused for the display & UV meter
    bitSet(playFlag, FLAG_REFRESH_DISPLAY);
  }
}

// Arduino Nano Timer notes:
// Each Timer/Counter has two output compare pins. (We are using OCR1A)
//  - Timer/Counter 0 OC0A and OC0B pins are called PWM pins 6 and 5 respectively.
//  - Timer/Counter 1 OC1A and OC1B pins are called PWM pins 9 and 10 respectively.
//  - Timer/counter 2 OC2A and OC2B pins are called PWM pins 11 and 3 respectively.
// Arduino libs provide 'F_CPU' as the clock frequency of the Arduino board you are compiling for.
#define MUSIC_FREQ 2000000  // value needs to be in Hz
// Configure Timer 1 to generate a square wave
void setupClockForAYChip() {
  TCCR1A = bit(COM1A0);             // Set Compare Output Mode to toggle pin on compare match
  TCCR1B = bit(WGM12) | bit(CS10);  // Set Waveform Generation Mode to Fast PWM and set the prescaler to 1
  // Calculate the value for the Output Compare Register A
  OCR1A = ((F_CPU / MUSIC_FREQ) / 2) - 1;  // =3 for 16mhz arduino
  pinMode(AY_Clock_pin, OUTPUT);           // Pin to output the clock signal
}

// Setup 8-bit timer2 to trigger interrupt (see ISR function)
void setupProcessLogicTimer() {
  cli();                // Disable interrupts
  TCCR2A = _BV(WGM21);  // CTC mode (Clear Timer and Compare)
  // 16000000Hz (ATmega16MHz) / 256 prescaler = 62500Hz (duty cycle)
  TCCR2B = _BV(CS22) | _BV(CS21);  // 256 prescaler  (CS22=1,CS21=1,CS20=0)
  OCR2A = DUTY_CYCLE_FOR_TIMER2;   // Set the compare value to control duty cycle
  TIFR2 |= _BV(OCF2A);             // Clear pending interrupts
  TIMSK2 = _BV(OCIE2A);            // Enable Timer 2 Output Compare Match Interrupt
  TCNT2 = 0;                       // Timer counter 2
  sei();                           // enable interrupts
  // https://onlinedocs.microchip.com/pr/GUID-93DE33AC-A8E1-4DD9-BDA3-C76C7CB80969-en-US-2/index.html?GUID-669CCBF6-D4FD-4E1D-AF92-62E9914559AA
}

void processPSG() {
  // Called by interrupt so keep this method as lightweight as possiblle
  byte action;
  while (readBuffer(action)) {

    switch (action) {
      // !!!!!!!!!! ?????? !!!!!!!!!!! this lines a issue for the tune : "Condomed Track 2.PSG"
      case END_OF_MUSIC_0xFD:
        bitSet(playFlag, FLAG_NEXT_TUNE);
        return;
      case END_OF_INTERRUPT_0xFF: return;
    }
    byte dat;
    if (readBuffer(dat)) {
      switch (action) {

        case PSG_REG_ENABLE:
          //case PSG_REG_IOA:
          //case PSG_REG_IOB:
          writeAY(action, dat | B11000000);  // enable sound bits - Forcing I/O portA, portB (B11000000) to be enabled
          LastAYEnableRegisiterUsed = dat;   // keep last used enabled bits, we can enable I/O later without losing sound bits
          break;

        case END_OF_INTERRUPT_MULTIPLE_0xFE:
          if ((dat == 0xff) && (fileRemainingBytesToRead / 32 == 0)) {
            // Some tunes have very long pauses at the end (caused by repeated sequences of "0xfe 0xff").
            // For example "NewZealandStoryThe.psg" has a very long pause at the end, I'm guessing by design to handover to the ingame tune.
            interruptCountSkip = 4;  // new replacement pause
          } else {
            interruptCountSkip = dat << 2;  //   x4, to make each a 80 ms wait - part of formats standard
          }
          return;  // do nothing in this cycle

        case PSG_REG_AMPLITUDE_A:
          lastPSGRegAmplitudeA = dat;  // restore levels after unpause
          writeAY(action, dat);
          break;
        case PSG_REG_AMPLITUDE_B:
          lastPSGRegAmplitudeB = dat;
          writeAY(action, dat);
          break;
        case PSG_REG_AMPLITUDE_C:
          lastPSGRegAmplitudeC = dat;
          [[fallthrough]];
        default:                 // 0x00 to 0xFC
          writeAY(action, dat);  // port & control regisiter
          break;                 // read more data - while loop
      }
    } else {
      circularBufferReadIndex--;  // CACHE WAS NOT READY - NOTHING HAPPEND, HOWEVER WE MUST REWIND THE ACTION OF FIRST readBuffer
      break;                      // leave now and allow main loop to start refilling cache!
    }
  }
}

// Reset AY chip to stop sound output
void resetAY() {
  audioMeanA = 0;
  audioMeanB = 0;
  audioMeanC = 0;

  setAYMode(INACTIVE);
  // Reset line needs to go High->Low->High for AY38910/12
  digitalWrite(ResetAY_pin, HIGH);  // just incase start high
  digitalWrite(ResetAY_pin, LOW);   // Reset pulse width must be min of 500ns
  digitalWrite(ResetAY_pin, HIGH);
  setAYMode(INACTIVE);

  writeAY(PSG_REG_ENABLE, B11000000);  // enable I/O
  writeAY(PSG_REG_IOA, 0);             // make sure VU meter is off
  writeAY(PSG_REG_IOB, 0);             // (nothing set so no LED's will come on yet)
}

// ------------------------------
// FILE - SD CARD SUPPORT SECION
// ------------------------------

// Returns true when data is waiting and ready on the cache.
inline bool isCacheReady() {
  return circularBufferReadIndex != circularBufferLoadIndex;
}

inline bool readBuffer(byte& dat) {
  if (isCacheReady()) {
    dat = playBuf[circularBufferReadIndex];
    ADVANCE_PLAY_BUFFER
    return true;
  } else {
    return false;
  }
}

// Helper function to check if a file has a .PSG extension
bool isPsgFile(const char* fileName) {
  return endsWithIgnoreCase(fileName, ".PSG");
}

// Reads a single byte from the SD card into the cache (circular buffer)
// Anything after EOF sets a END_OF_MUSIC_0xFD command into the cache
// which will trigger a FLAG_PLAY_NEXT_TUNE.
void cacheSingleByteRead() {

  if (file) {
    if (circularBufferLoadIndex == circularBufferReadIndex - 1)  // Check if there is enough space in the buffer to write a new byte
      return;                                                    // There is no space in the buffer, so exit the function

    // Special case: Check if the circular buffer is full and the read index is at the beginning.
    if (circularBufferLoadIndex == (BUFFER_SIZE - 1) && circularBufferReadIndex == 0)
      return;

    if (fileRemainingBytesToRead >= 1) {  // Read a byte from the file and store it in the circular buffer
      playBuf[circularBufferLoadIndex] = file.read();
      fileRemainingBytesToRead--;
    } else {
      // There is no more data in the file, so write the end-of-music byte instead
      playBuf[circularBufferLoadIndex] = END_OF_MUSIC_0xFD;
    }
    // Increment the circular buffer load index.
    ADVANCE_LOAD_BUFFER  // Note: using byte will wrap around to zero automatically
  }
}

// Helper function to write a 32-bit integer to the output file
void writeUint32(File& file, uint32_t value) {
  file.write((uint8_t)(value & 0xFF));
  file.write((uint8_t)((value >> 8) & 0xFF));
  file.write((uint8_t)((value >> 16) & 0xFF));
  file.write((uint8_t)((value >> 24) & 0xFF));
}

// Helper function to write a 16-bit integer to the output file
void writeUint16(File& file, uint16_t value) {
  file.write((uint8_t)(value & 0xFF));
  file.write((uint8_t)((value >> 8) & 0xFF));
}

// Helper function 
bool endsWithIgnoreCase(const char* str, const char* suffix) {
  size_t strLen = strlen(str);
  size_t suffixLen = strlen(suffix);
  if (strLen < suffixLen) return false;

  const char* strEnd = str + strLen - suffixLen;
  return strcasecmp(strEnd, suffix) == 0;
}

// Helper function 
const char* getFileNameByIndex(File& tempFile, size_t index) {
  uint32_t offset;
  uint16_t length;

  // Calculate the position in the header for the given index
  tempFile.seek(4 + index * 6);

  // Read the offset directly as a 4-byte integer
  offset = static_cast<uint32_t>(tempFile.read()) | (static_cast<uint32_t>(tempFile.read()) << 8) | (static_cast<uint32_t>(tempFile.read()) << 16) | (static_cast<uint32_t>(tempFile.read()) << 24);

  // Read the length directly as a 2-byte integer
  length = static_cast<uint16_t>(tempFile.read()) | (static_cast<uint16_t>(tempFile.read()) << 8);

  // Seek to the file name position
  tempFile.seek(offset);

  // Read the file name
  static char fileName[8 + 4 + 1];
  fileName[length] = '.';
  fileName[length + 1] = 'P';
  fileName[length + 2] = 'S';
  fileName[length + 3] = 'G';

  tempFile.read((uint8_t*)fileName, length);
  fileName[length + 4] = '\0';  // Null-terminate the string

  //return String(fileName);
  return fileName;
}

/*
Here's how the header and filenames are structured:
  Header:
    4 bytes, Total file count
    4 bytes, Offset of the filename
    2 bytes, Length of the filename
  Filenames:
    N bytes, filenames sequentially, no nulls
*/

size_t CreatePsgFileList(File& outputFile) {
  // --- First Pass: Count the total number of .PSG files ---
  uint32_t totalFiles = 0;
  File entry = root.openNextFile();
  while (entry) {
    if (!entry.isDirectory() && isPsgFile(entry.name())) {
      totalFiles++;
    }
    entry.close();
    entry = root.openNextFile();
  }

  if (totalFiles == 0) {
    return 0; // No .PSG files found
  }

  // Calculate header size: 4 bytes for total count, plus 6 bytes for each file (offset + length)
  const size_t headerEntrySize = 4 + 2; // 4 bytes for offset, 2 bytes for length
  const size_t headerSize = 4 + (totalFiles * headerEntrySize); // 4 bytes for total count

  // --- Second Pass: Write Header Part ---
  root.rewindDirectory(); // Restart the directory traversal
  uint32_t currentOffset = headerSize; // Start writing strings after the header
  outputFile.seek(0);

  // Write total file count at the start of the header
  writeUint32(outputFile, totalFiles);

  entry = root.openNextFile();
  while (entry) {
    if (!entry.isDirectory() && isPsgFile(entry.name())) {
      size_t fileNameLength = strlen(entry.name()) - 4; // Exclude ".PSG" from the length
      writeUint32(outputFile, currentOffset);           // Write offset (4 bytes)
      writeUint16(outputFile, fileNameLength);          // Write length (2 bytes)
      currentOffset += fileNameLength;                  // Update the current offset for the next file name
    }
    entry.close();
    entry = root.openNextFile();
  }

  // --- Third Pass: Write the file names ---
  root.rewindDirectory();
  entry = root.openNextFile();
  while (entry) {
    if (!entry.isDirectory() && isPsgFile(entry.name())) {
      // Remove ".PSG" from the file name before writing
      char fileName[13]; // Assuming 8.3 filename format
      strncpy(fileName, entry.name(), strlen(entry.name()) - 4);
      fileName[strlen(entry.name()) - 4] = '\0'; // Null-terminate the string

      // Write the file name to the output file
      outputFile.print(fileName);
    }
    entry.close();
    entry = root.openNextFile();
  }

  return totalFiles;
}


uint8_t readValidPSGFileHeader() {
  constexpr uint8_t PSG_HEADER_SIZE = 16;
  if (file && !file.isDirectory() && file.available() >= PSG_HEADER_SIZE) {
    if (file.read() == 'P' && file.read() == 'S' && file.read() == 'G' && file.read() == 0x1A) { 
      file.seek(PSG_HEADER_SIZE - 4);
      return PSG_HEADER_SIZE;
    }
  }
  return 0; // Invalid PSG file
}

bool initTune(uint16_t currentIndex) {

  resetAY();
  RESET_BUFFERS

  const char* fileName = getFileNameByIndex(root,currentIndex);
  if (file) { file.close(); }
  file = SD.open(fileName);

  // Check if the file has a valid PSG header
  uint8_t readBytes = readValidPSGFileHeader();
  fileRemainingBytesToRead = file.size() - readBytes;
  if (readBytes == 0) {
  //  bitSet(playFlag, FLAG_NOT_PSG_FILE_ERROR);
    file.close(); // Close invalid file
    return false;
  }
  return true;
}

/* 
=======================================================================
                      USEFUL NOTES SECTION
=======================================================================
Here are some playback rates for various architectures:-
  - Amstrad CPC: 1 MHz
  - Atari ST: 2 MHz
  - MSX: 1.7897725 MHz
  - Oric-1: 1 MHz
  - ZX Spectrum: 1.7734 MHz
=======================================================================

=== PSG FILE - HEADER & DATA DETAILS ===
-----------------------------------------------------------------
Offset   Bytes Used   Description
-----------------------------------------------------------------
[0]      3            Identifier test = "PSG"
[3]      1            "end-of-text" marker (1Ah)
[4]      1            Version number
[5]      1            Playback frequency (for versions 10+)
[6]      10           Unknown
-----------------------------------------------------------------

Example of PSG File Header(16 bytes), header is followed by the start of the raw byte data
HEADER: 50 53 47 1A 00 00 00 00 00 00 00 00 00 00 00 00
DATA  : FF FF 00 F9 06 16 07 38 FF 00 69 06 17 FF 00 F9 ...

=== PSG PAYLOAD/DATA DETAILS ===
PSG commands - music format for byte data from file (payload/body/data)
- END_OF_INTERRUPT_0xFF           [0xff]              : End of interrupt (EOI) - waits for 20 ms
- END_OF_INTERRUPT_MULTIPLE_0xFE  [0xfe],[byte]       : Multiple EOI, following byte provides how many times to wait 80ms (value of "1" is x4 longer that FF).
- END_OF_INTERRUPT_MULTIPLE_0xFE  [0xfd]              : End Of Music
- END_OF_MUSIC_0xFD               [0x00..0x0f],[byte] : PSG register, following byte is accompanying data for this register

note: Value register numbers range from 16 to 252 
      (Ranges 0x10 to 0xfc don't exist for the AY3-891x chips and should be ignored)

=======================================================================

# Reference taken from the "AY-3-8910-datasheet" - section 3.6
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

