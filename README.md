# Ranging with DW1000

## Components

- Project root
   - `tagArduino/`: Arduino sketch for tag which receives command from Raspberry Pi and controls DW1000
   - `tagRPi/`: Raspberry Pi C++ program which sends command to `tagArduino` and receives result
   - `anchorArduino/`: Arduino sketch for anchor which controls DW1000
   - `def.h`: Common definition file used by both tag and anchor
   - `dwm1000.h`: DW1000-specific definitions used by Arduino
   - `i2c.h`: tag-specific definitions used for I2C communication

You can ignore other files

## Prepare

### Tools and Libraries

- Install [Arduino IDE](https://www.arduino.cc/en/Main/Software)
- Arduino DW1000: Visit https://github.com/thotro/arduino-dw1000 and follow [`Installation`](https://github.com/thotro/arduino-dw1000#installation) instruction
- Raspberry Pi C++ WiringPi: `[sudo] apt-get install wiringpi`

### Header files

- `def.h`: Copy to all directories
- `dwm1000.h`: Copy to `*Arduino` directories
- `i2c.h`: Copy to `tag*` directories

## Build

Go to [tagArduino](tagArduino), [tagRPi](tagRPi), and [anchorArduino](anchorArduino) and follow instruction of each
