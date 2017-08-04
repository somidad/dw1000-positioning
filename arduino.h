/*
 * NOTE: Copy or (sym/hard)link this to the correspond directory
 */

#ifndef ARDUINO_COMMON_H
#define ARDUINO_COMMON_H

// pin config
#define PIN_RST 9
#define PIN_IRQ 2
#define PIN_SS SS

#define LOGTIME Serial.print(millis()); Serial.print(" ");
#define LOG(var) (Serial.print(var))
#define LOGF(str) (Serial.print(F(str)))
#define LOGLN(var) (Serial.println(var))
#define LOGFLN(str) (Serial.println(F(str)))

#endif /* ARDUINO_COMMON_H */

