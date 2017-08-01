/*
 * NOTE: Copy or (sym/hard)link this to the correspond directory
 */

#ifndef ARDUINO_COMMON_H
#define ARDUINO_COMMON_H

// pin config
#define PIN_RST 9
#define PIN_IRQ 2
#define PIN_SS SS
// message types
#define POLL 0
#define POLL_ACK 1
#define RANGE 2
#define RANGE_REPORT 3
#define BEACON 127
#define RANGE_FAILED 255

#define LEN_DATA 20

#define REPLY_DELAY_MS 3
#define TIMEOUT_PERIOD_MS 100
#define BEACON_PERIOD_MS 500
#define BEACON_JITTER_MS 10
#define UPDATE_LAST_BEACON (lastBeacon = millis())

#define TAG_NONE 0
#define MASK_ANCHOR (1 << 15)
#define MASK_RIGHT (1 << 14)
#define MASK_LEFT (1 << 14)

#define ADDR_SIZE 2
#define GET_TYPE(buffer) (buffer[0])
#define SET_TYPE(buffer, type) (buffer[0] = type)
#define GET_SOURCE(buffer, nodeId) (memcpy(&nodeId, buffer + 1, ADDR_SIZE))
#define SET_SOURCE(buffer, nodeId) (memcpy(buffer + 1, &nodeId, ADDR_SIZE))
#define SET_DEST(buffer, nodeId) (memcpy(buffer + 3, &nodeId, ADDR_SIZE))
#define DOES_MATCH_SOURCE(buffer, nodeId) (!memcmp(buffer + 1, &nodeId, ADDR_SIZE))
#define DOES_MATCH_DEST(buffer, nodeId) (!memcmp(buffer + 3, &nodeId, ADDR_SIZE))

#define LOGTIME Serial.print(millis()); Serial.print(" ");
#define LOG(var) (Serial.print(var))
#define LOGF(str) (Serial.print(F(str)))
#define LOGLN(var) (Serial.println(var))
#define LOGFLN(str) (Serial.println(F(str)))

#endif
