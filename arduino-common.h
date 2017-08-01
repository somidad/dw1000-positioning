/*
 * NOTE: Copy or symlink this to the correspond directory
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
#define SET_TYPE(type) (txBuffer[0] = type)
#define GET_SOURCE(nodeId) (memcpy(&nodeId, rxBuffer + 1, ADDR_SIZE))
#define SET_SOURCE(nodeId) (memcpy(txBuffer + 1, &nodeId, ADDR_SIZE))
#define SET_DEST(nodeId) (memcpy(txBuffer + 3, &nodeId, ADDR_SIZE))
#define DOES_MATCH_SOURCE(nodeId) (!memcmp(rxBuffer + 1, &nodeId, ADDR_SIZE))
#define DOES_MATCH_DEST(nodeId) (!memcmp(rxBuffer + 3, &nodeId, ADDR_SIZE))

#endif