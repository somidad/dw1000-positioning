#include <Wire.h>
#include <SPI.h>

#include <DW1000.h>

#include "def.h"
#include "i2c.h"
#include "dwm1000.h"

#define PIN_IRQ  2
#define PIN_RST  9
#define PIN_SS  SS

/* Edit tagId */
const uint16_t tagId = 1;
const uint16_t networkId = 10;

#define NUM_ANCHORS 5

char cmd = CMD_NONE;
char state = STATE_IDLE;
char type = TYPE_NONE;

unsigned char num_anchors = 0;
unsigned char idx_anchor = 0;
uint16_t anchorId[NUM_ANCHORS] = {ID_NONE, ID_NONE, ID_NONE, ID_NONE, ID_NONE};
float distance[NUM_ANCHORS] = {0, 0, 0, 0, 0};

DW1000Time reply_delay = DW1000Time(REPLY_DELAY_MS, DW1000Time::MILLISECONDS);
DW1000Time timePollSent;
DW1000Time timePollReceived;
DW1000Time timePollAckSent;
DW1000Time timePollAckReceived;
DW1000Time timeRangeSent;
DW1000Time timeRangeReceived;

unsigned long curMillis;
unsigned long lastSent;

byte txBuffer[FRAME_LEN];
byte rxBuffer[FRAME_LEN];

boolean sentFrame = false;
boolean receivedFrame = false;

/***********************************************
 * I2C Raspberry Pi (master) - Arduino (slave) *
 ***********************************************/
void i2cReceiveEvent(int bytes) {
  if (!bytes) {
    return;
  }
  while (Wire.available()) {
    cmd = Wire.read();
  }
  if (cmd == CMD_SCAN && state == STATE_IDLE) {
    state = STATE_SCAN;
    return;
  }
  if (cmd == CMD_DATA_READY) {
    type = TYPE_DATA_READY;
    return;
  }
  if (cmd == CMD_TYPE_ID) {
    type = TYPE_ID;
    return;
  }
  if (cmd == CMD_TYPE_DIST) {
    type = TYPE_DIST;
    return;
  }
}

void i2cRequestEvent() {
  if (state != STATE_IDLE || type == TYPE_NONE) {
    Wire.write(I2C_NODATA);
    return;
  }
  if (type == TYPE_DATA_READY) {
    Wire.write(I2C_DATARD);
    return;
  }
  if (type == TYPE_ID) {
    Wire.write((byte*)anchorId, 2 * NUM_ANCHORS);
    return;
  }
  if (type == TYPE_DIST) {
    Wire.write((byte*)distance, 4 * NUM_ANCHORS);
    return;
  }
}

void setupI2C() {
  // 7-bit addressing
  // ref: table 3, page 17, http://www.nxp.com/docs/en/user-guide/UM10204.pdf
  Wire.begin(0x04);
  Wire.onRequest(i2cRequestEvent);
  Wire.onReceive(i2cReceiveEvent);
  type = TYPE_NONE;
}

/*************************************
 * Arduino (master) - DW1000 (slave) *
 *************************************/
void spiReceiveEvent() {
  receivedFrame = true;
}

void spiSendEvent() {
  sentFrame = true;
}

void initDW1000Receiver() {
  DW1000.newReceive();
  DW1000.setDefaults();
  DW1000.receivePermanently(true);
  DW1000.startReceive();  
}

void setupDW1000() {
  DW1000.begin(PIN_IRQ, PIN_RST);
  DW1000.select(PIN_SS);
  DW1000.newConfiguration();
  DW1000.setDefaults();
  DW1000.setNetworkId(networkId);
  DW1000.setDeviceAddress(tagId);
  DW1000.enableMode(DW1000.MODE_LONGDATA_RANGE_LOWPOWER);
  DW1000.commitConfiguration();

  DW1000.attachSentHandler(spiSendEvent);
  DW1000.attachReceivedHandler(spiReceiveEvent);

  initDW1000Receiver();
}

void transmitPing() {
  DW1000.newTransmit();
  DW1000.setDefaults();
  txBuffer[0] = FTYPE_PING;
  SET_SRC(txBuffer, tagId, ADDR_SIZE);
  DW1000.setData(txBuffer, FRAME_LEN);
  DW1000.startTransmit();
}

void transmitPoll() {
  DW1000.newTransmit();
  DW1000.setDefaults();
  txBuffer[0] = FTYPE_POLL;
  SET_SRC(txBuffer, tagId, ADDR_SIZE);
  SET_DST(txBuffer, anchorId[idx_anchor], ADDR_SIZE);
  DW1000.setData(txBuffer, FRAME_LEN);
  DW1000.startTransmit();
}

void transmitRange() {
  DW1000.newTransmit();
  DW1000.setDefaults();
  txBuffer[0] = FTYPE_RANGE;
  SET_SRC(txBuffer, tagId, ADDR_SIZE);
  SET_DST(txBuffer, anchorId[idx_anchor], ADDR_SIZE);
  DW1000.setDelay(reply_delay);
  DW1000.setData(txBuffer, FRAME_LEN);
  DW1000.startTransmit();
}

void calculateRange() {
    // asymmetric two-way ranging (more computation intense, less error prone)
    DW1000Time round1 = (timePollAckReceived - timePollSent).wrap();
    DW1000Time reply1 = (timePollAckSent - timePollReceived).wrap();
    DW1000Time round2 = (timeRangeReceived - timePollAckSent).wrap();
    DW1000Time reply2 = (timeRangeSent - timePollAckReceived).wrap();
    DW1000Time tof = (round1 * round2 - reply1 * reply2) / (round1 + round2 + reply1 + reply2);
    distance[idx_anchor] = tof.getAsMeters();
}

/********
 * Main *
 ********/
void setup() {
  setupI2C();
  setupDW1000();
}

void loop() {
  curMillis = millis();
  if (state == STATE_PONG && curMillis - lastSent > PONG_TIMEOUT_MS) {
    if (num_anchors < 3) {
      state = STATE_IDLE;
      return;
    } else {
      idx_anchor = 0;
      state = STATE_ROUNDROBIN;
      return;
    }
  }
  if (state == STATE_POLLACK && curMillis - lastSent > POLLACK_TIMEOUT_MS) {
    idx_anchor++;
    state = STATE_ROUNDROBIN;
    return;
  }
  if (state == STATE_RANGEREPORT && curMillis - lastSent > RANGEREPORT_TIMEOUT_MS) {
    idx_anchor++;
    state == STATE_ROUNDROBIN;
    return;
  }

  if (state == STATE_SCAN) {
    for (idx_anchor = 0; idx_anchor < NUM_ANCHORS; idx_anchor++) {
      anchorId[idx_anchor] = ID_NONE;
      distance[idx_anchor] = 0;
    }
    num_anchors = 0;
    transmitPing();
    lastSent = millis();
    state = STATE_PONG;
    return;
  }

  if (state == STATE_ROUNDROBIN) {
    if (idx_anchor < num_anchors) {
      transmitPoll();
      lastSent = millis();
      state = STATE_POLLACK;
    } else {
      state = STATE_IDLE;
    }
    return;
  }

  if (sentFrame) {
    sentFrame = false;
    if (txBuffer[0] == FTYPE_POLL) {
      DW1000.getTransmitTimestamp(timePollSent);
    }
    if (txBuffer[0] == FTYPE_RANGE) {
      DW1000.getTransmitTimestamp(timeRangeSent);
    }
  }

  if (receivedFrame) {
    receivedFrame = false;
    DW1000.getData(rxBuffer, FRAME_LEN);

    if (state == STATE_PONG) {
      if (rxBuffer[0] != FTYPE_PONG) {
        return;
      }
      if (!DOES_DST_MATCH(rxBuffer, tagId, ADDR_SIZE)) {
        return;
      }
      #warning "This may store anchors with the same ID"
      memcpy(&anchorId[idx_anchor], rxBuffer + 1, ADDR_SIZE);
      num_anchors++;
      return;
    }

    if (state == STATE_POLLACK) {
      if (rxBuffer[0] != FTYPE_POLLACK) {
        return;
      }
      if (!DOES_SRC_MATCH(rxBuffer, anchorId[idx_anchor], ADDR_SIZE)) {
        return;
      }
      if (!DOES_DST_MATCH(rxBuffer, tagId, ADDR_SIZE)) {
        return;
      }
      DW1000.getReceiveTimestamp(timePollAckReceived);
      transmitRange();
      state = STATE_RANGEREPORT;
      lastSent = millis();
      return;
    }

    if (state == STATE_RANGEREPORT) {
      if (rxBuffer[0] != FTYPE_RANGEREPORT) {
        return;
      }
      if (!DOES_SRC_MATCH(rxBuffer, anchorId[idx_anchor], ADDR_SIZE)) {
        return;
      }
      if (!DOES_DST_MATCH(rxBuffer, tagId, ADDR_SIZE)) {
        return;
      }
      timePollReceived.setTimestamp(rxBuffer + 5);
      timePollAckSent.setTimestamp(rxBuffer + 10);
      timeRangeReceived.setTimestamp(rxBuffer + 15);
      calculateRange();
      state = STATE_ROUNDROBIN;
      return;
    }
  }
}
