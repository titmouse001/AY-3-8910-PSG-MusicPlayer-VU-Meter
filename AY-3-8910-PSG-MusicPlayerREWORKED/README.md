## Arduino Code - AY-3-8910 PSG Music Player with VU Meter

#### Main Functions:
- **setup()**: Initializes the OLED, AY-3-8910, SD card, and timers.
- **loop()**: Handles button inputs, playback control, and display updates.
- **processPSG()**: Processes PSG data from the file and sends it to the AY-3-8910.
- **updateDisplay()**: Updates the OLED with current file info and VU meter.
- **handlePlayback()**: Manages the playback of PSG files, including loading data into a circular buffer.

#### Optimisations:
- **Circular Buffer**: Used to manage PSG data.
- **Timer Interrupts**: Used for ISR timing - for music playback and display.
- **Memory Management**: Optimized to fit into the memory of the ATmega168.

#### TODOs:
- Add user options/setttings
- Random tune playback
- Autoplay new tune
- Option to prescan for invlid PSG files

#### Dependencies:
***Libraries:*** SPI.h, SD.h, SSD1306AsciiAvrI2c.h, and my custom headers for pins and AY-3-8910 registers.
