## AY-3-8910-PSG-MusicPlayer-VU-Meter

### Music Player Release
This iteration features a reduced hardware design and an enhanced user interface.

#### Hardware Used
- Arduino Nano
- AY3-8910
- OLED
- SD Card Reader
- Buttons (x4)
- Audio Jack

The Arduino Nano is a small, powerful microcontroller board based on the ATmega328P chip. Similar to Arduino Uno, but smaller and more affordable. It has 14 digital I/O, 8 analog inputs, 16 MHz clock speed, USB connectivity and it's easy to program using the Arduino IDE. 
The Nano is used to drive an AY-3 sound chip by connecting the necessary pins from the Nano to the AY-3 chip. The Nano controls the AY-3's sound output by sending control signals to the chip via digital pins. The OLED display and SD card reader are connected to the Nano using the I2C and SPI communication protocols, respectively. The 4 input buttons use a single pin on the Nano, with the use of resistor ladder to provide different 'analog voltages' to the Nano when the buttons are pressed allowing different actions.

An OLED is a type of OLED (organic light-emitting diode) display that can be used with an Arduino microcontroller board. The display has a resolution of 32x128 pixels, which means it can display 32 columns of 128 rows. OLED displays are known for their high contrast, fast response time and wide viewing angles, they are also energy efficient as they don't require a backlight.  It can be connected using the I2C communication protocol, which only requires 4 wires using the SDA and SCL pins including VCC and GND pins to provide power to the OLED display.

#### A Brief History of the AY-3 Programmable Sound Generator (PSG) Chip
1977: General Instrument (GI) introduces the AY-3-891x series of PSG chips as a low-cost alternative to the Yamaha YM2149F PSG.
Late 1970s to early 1980s: AY-3-891x chips are widely used in home computers, game consoles, and arcade games due to their low cost and good sound quality.

#### Overview of the AY-3-891x
- 3 audio channels
- Square wave, white noise, and tone generation
- Envelope generator for sound control (attack, decay, sustain, release)
- Low cost alternative to Yamaha YM2149F
- Widely used in home computers, game consoles and arcade games

#### PCB 
<img src="/AY-3-8910-PSG-MusicPlayerREWORKED/Pictures_Prototyping/Photo View_2023-01-25.svg" width="45%" />

#### Breadboard Prototype (top view)
<img src="/AY-3-8910-PSG-MusicPlayerREWORKED/Pictures_Prototyping/BreadboardPrototypePic1.png" width="45%" />

#### Breadboard Prototype (side view)
<img src="/AY-3-8910-PSG-MusicPlayerREWORKED/Pictures_Prototyping/BreadboardPrototypePic2.png" width="45%" />

----
_Research and resources used in the development of this project_ :  
[Resistor Ladder](https://github.com/bxparks/AceButton/blob/develop/docs/resistor_ladder/README.md)

