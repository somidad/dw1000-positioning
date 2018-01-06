# ToA based positioning with DW1000 UWB module

## Components

- Project root
   - `tagArduino/`: Arduino sketch for tag which receives command from Raspberry Pi and controls DW1000
   - `tagRPi/`: Raspberry Pi C++ program which sends command to `tagArduino` and receives result
   - `anchorArduino/`: Arduino sketch for anchor which controls DW1000
   - `headers`: Common header files are located here

You can ignore other files

## Instructions

Navigate `README.md` in each directory

- [tagArduino](tagArduino)
- [tagRPi](tagRPi)
- [anchorArduino](anchorArduino)
