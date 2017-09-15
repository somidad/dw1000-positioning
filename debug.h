#ifndef DEBUG_H
#define DEBUG_H

#if DEBUG
  #include <SPI.h>
  #define PRINT(x)   Serial.print(x)
  #define PRINTLN(x) Serial.println(x)
#else // DEBUG
  #define PRINT(x)
  #define PRINTLN(x)
#endif // DEBUG

#endif DEBUG_H