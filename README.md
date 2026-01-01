# Futaba-LCD-Emulator-Controller
A vibe-coded interface for Arduino Uno compatible and the Futaba NA162SD07AA LCD Emulator VFD devices.
These VFDs use 5v logic, so I had to use my Adafruit Metro 328, an Arduino Uno compatible dev board.

![Startup animation on the VFD](https://github.com/drive-c/Futaba-LCD-Emulator-Controller/blob/main/demo/VFD.gif)
![Screenshot of the console interface](https://github.com/drive-c/Futaba-LCD-Emulator-Controller/blob/main/demo/Console.png)

The product spec sheet for the Futaba LCD Emulator line can be found [here](https://www.futaba.com/wp-content/uploads/2019/04/LCD_Emulators.pdf). My model and my code are assuming synchronous serial interface (SPI) and English/Katakana character set. It's not possible to change the character set, but switching from parallel to serial is possible by soldering specific jumpers as listed in the product specsheet.

The Adafruit Metro series of devices are available [here](https://www.adafruit.com/category/818). This VFD requires 5-volt logic, so you either need a board that does 5-volt logic or wire it with a logic level converter.

This project is built specifically for the 16x2 NA162SD07AA model that I have. There are other column and row sizes available and it would be fairly easy to change the code to use these VFDs.

# Pin Layout #
For the Futaba pins from *right to left*:
Pin 1 - Ground
Pin 2 - 5V
Pin 3 - Pin 11
Pin 4 - Pin 10
Pin 5 - Unused/RST (This depends on your VFD model and configuration)
Pin 6 - Pin 13

## TODO: ##
- Add images of the working VFD
- Add screenshots of terminal emulator
- ~~Add links to VFD specs~~
- ~~Add links to Adafruit Metro 328 and Arduino Uno~~
