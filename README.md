## AY-3-8910-PSG-MusicPlayer-REWORKED

### Music Player Release
This iteration features a reduced hardware design and an enhanced user interface.

#### Hardware Used
- Arduino Nano
- AY3-8910
- OLED
- SD Card Reader
- Buttons (x4)
- Audio Jack

Arduino Nano is a small, powerful microcontroller board based on the ATmega328P chip. Similar to Arduino Uno, but smaller and more affordable. It has 14 digital I/O, 8 analog inputs, 16 MHz clock speed, USB connectivity and it's easy to program using the Arduino IDE. 
The Nano is used to drive an AY-3 sound chip by connecting the necessary pins from the Nano to the AY-3 chip. The Nano controls the AY-3's sound output by sending control signals to the chip via digital pins. The OLED display and SD card reader are connected to the Nano using the I2C and SPI communication protocols, respectively. The 4 input buttons use a single pin on the Nano, with the use of resistors to provide different 'analog voltages' to the Nano when the buttons are pressed allowing different actions.

An OLED 32x128 for Arduino is a type of OLED (organic light-emitting diode) display that can be used with an Arduino microcontroller board. The display has a resolution of 32x128 pixels, which means it can display 32 columns of 128 rows. OLED displays are known for their high contrast, fast response time and wide viewing angles, they are also energy efficient as they don't require a backlight.
The OLED 32x128 for Arduino can be connected to the Arduino board using the I2C communication protocol, which only requires 4 wires to establish communication. Once connected, the Arduino can be programmed to display text, images, or other data on the OLED screen. To use the OLED display with an Arduino, you will need to install a library that provides the necessary functions for controlling the display. Some popular libraries include the Adafruit_SSD1306 and the U8g2 libraries.

The OLED display and Arduino board communicate using I2C communication protocol, these 4 wires provide all the necessary signals for the communication to happen. The SDA and SCL pins on the Arduino board are used for communication with I2C devices, such as the OLED display. The VCC and GND pins provide power to the OLED display.


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
