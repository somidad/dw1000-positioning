# tagArduino

## State Diagram

![](../_state-diagram/tag.png)

## Prerequisite

### `arduino-dw1000`

Install [`arduino-dw1000`](https://github.com/thotro/arduino-dw1000) library on your computer (which Arduino IDE runs)

## Installing `tagArduino`

- Copy or link `def.h`, `dwm1000.h`, `i2c.h` from the parent directory to here
- Edit `I2CSLAVEADDR` in `i2c.h` if needed

Compile and upload via Arduino IDE
