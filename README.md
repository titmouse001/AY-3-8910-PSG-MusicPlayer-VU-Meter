## AY-3-8910 PSG Music Player with VU Meter

### Music Player Release
This version features a streamlined hardware design and an improved user interface.

#### Hardware Used
- Arduino Nano
- AY3-8910
- OLED
- SD Card Reader
- Buttons (x4)
- Audio Jack

The **Arduino Nano** is a small yet powerful microcontroller board based on the ATmega328P chip. Similar to the Arduino Uno but smaller and more affordable, it features 14 digital I/O pins, 8 analog inputs, a 16 MHz clock speed, and USB connectivity. It is easy to program using the Arduino IDE. In this project, the Nano drives the AY-3 sound chip by connecting its digital pins to the chip's control inputs. The Nano sends control signals to the AY-3 to produce sound.

The **OLED display** and **SD card reader** are connected to the Nano using the I2C and SPI communication protocols, respectively. The four input buttons share a single Nano pin through a resistor ladder, which generates distinct analog voltages for each button press. This enables multiple actions, such as play, pause, and select, with minimal hardware.

The **OLED display** has a resolution of 32x128 pixels, enabling it to display text and simple graphics. OLEDs are known for their high contrast, fast response times, wide viewing angles, and energy efficiency, as they do not require a backlight. The display connects to the Nano via the I2C protocol, which requires only four wires (SDA, SCL, VCC, and GND).

#### A Brief History of the AY-3 Programmable Sound Generator (PSG) Chip
- **1977:** General Instrument (GI) introduced the AY-3-891x series of PSG chips as a low-cost alternative to the Yamaha YM2149F.
- **Late 1970s to early 1980s:** AY-3-891x chips were widely used in home computers, game consoles, and arcade games due to their affordability and good sound quality.

#### Overview of the AY-3-891x PSG
- Three independent audio channels
- Supports square wave, white noise, and tone generation
- Built-in envelope generator for sound control (attack, decay, sustain, release)
- Affordable alternative to the Yamaha YM2149F
- Widely used in vintage home computers, game consoles, and arcade games

The AY-3-8910 remains a popular chip among retro computing and gaming enthusiasts due to its versatility and distinctive sound capabilities.

#### PCB 
<img src="/AY-3-8910-PSG-MusicPlayerREWORKED/Pictures_Prototyping/Photo View_2023-01-25.svg" width="45%" />

#### Breadboard Prototype (top view)
<img src="/AY-3-8910-PSG-MusicPlayerREWORKED/Pictures_Prototyping/BreadboardPrototypePic1.png" width="45%" />

#### Breadboard Prototype (side view)
<img src="/AY-3-8910-PSG-MusicPlayerREWORKED/Pictures_Prototyping/BreadboardPrototypePic2.png" width="45%" />

----
_Research and resources used in the development of this project_ :  
[Resistor Ladder](https://github.com/bxparks/AceButton/blob/develop/docs/resistor_ladder/README.md)

