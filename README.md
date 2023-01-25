## AY-3-8910-PSG-MusicPlayer-REWORKED

### Music Player Release
This iteration features a reduced hardware design and an enhanced user interface.

An Arduino Nano can be used to drive an AY-3 sound chip by connecting the necessary pins from the Nano to the AY-3 chip. The Nano can control the AY-3's sound output by sending control signals to the chip via digital pins. The OLED display and SD card reader can be connected to the Nano using the I2C and SPI communication protocols, respectively. The input buttons can also be connected to a single pin on the Nano, with the use of resistors to provide different analog voltages to the Nano when the buttons are pressed. This allows the Nano to read the button presses and perform different actions based on the input.

#### What's a Arduino Nano
Arduino Nano is a small, powerful microcontroller board based on the ATmega328P chip. Similar to Arduino Uno, but smaller and more affordable. It has 14 digital I/O, 8 analog inputs, 16 MHz clock speed, USB connectivity and it's easy to program using the Arduino IDE. 

#### A Brief History of the AY-3 Programmable Sound Generator (PSG) Chip
1977: General Instrument (GI) introduces the AY-3-891x series of PSG chips as a low-cost alternative to the Yamaha YM2149F PSG.
Late 1970s to early 1980s: AY-3-891x chips are widely used in home computers, game consoles, and arcade games due to their low cost and good sound quality.

#### Overview of the AY-3-891x
- 3 audio channels
- Square wave, white noise, and tone generation
- Envelope generator for sound control (attack, decay, sustain, release)
- Low cost alternative to Yamaha YM2149F
- Widely used in home computers, game consoles and arcade games

## Breadboard Prototype - top view
![OLED](/AY-3-8910-PSG-MusicPlayerREWORKED/Pictures_Prototyping/BreadboardPrototypePic1.png)  

## Breadboard Prototype - side view
![OLED](/AY-3-8910-PSG-MusicPlayerREWORKED/Pictures_Prototyping/BreadboardPrototypePic2.png) 
