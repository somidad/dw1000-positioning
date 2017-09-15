#define DEBUG true

#include <Wire.h>
#include <SPI.h>

#include <DW1000.h>

#include "debug.h"
#include "def.h"
#include "i2c.h"
#include "dwm1000.h"

#define PIN_IRQ  2
#define PIN_RST  9
#define PIN_SS  SS

/* Edit tagId */
const uint16_t tagId = FLAG_TAG | 1;
const uint16_t networkId = 10;

volatile char cmd = CMD_NONE;
volatile char state = STATE_IDLE;
volatile char type = TYPE_NONE;

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
unsigned long lastStateChange;

byte txBuffer[FRAME_LEN];
byte rxBuffer[FRAME_LEN];

boolean sentFrame = false;
boolean receivedFrame = false;

void updateState(int nextState) {
  state = nextState;
  lastStateChange = millis();
}

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
    PRINTLN(F("Transitting to SCAN state..."));
    updateState(STATE_SCAN);
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
  Wire.begin(I2CSLAVEADDR);
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
  DW1000.enableMode(DW1000.MODE_LONGDATA_RANGE_ACCURACY);
  DW1000.commitConfiguration();

  DW1000.attachSentHandler(spiSendEvent);
  DW1000.attachReceivedHandler(spiReceiveEvent);

  initDW1000Receiver();
}

void prepareTx() {
  DW1000.newTransmit();
  DW1000.setDefaults();
}

void startTx() {
  DW1000.setData(txBuffer, FRAME_LEN);
  DW1000.startTransmit();
  // timeout will be asserted after tx interrupt
  lastSent = 0;
}

void transmitPing() {
  prepareTx();
  txBuffer[0] = FTYPE_PING;
  SET_SRC(txBuffer, tagId, ADDR_SIZE);
  startTx();
}

void transmitPoll() {
  prepareTx();
  txBuffer[0] = FTYPE_POLL;
  SET_SRC(txBuffer, tagId, ADDR_SIZE);
  SET_DST(txBuffer, anchorId[idx_anchor], ADDR_SIZE);
  startTx();
}

void transmitRange() {
  prepareTx();
  txBuffer[0] = FTYPE_RANGE;
  SET_SRC(txBuffer, tagId, ADDR_SIZE);
  SET_DST(txBuffer, anchorId[idx_anchor], ADDR_SIZE);
  DW1000.setDelay(reply_delay);
  startTx();
}

void calculateRange() {
    // asymmetric two-way ranging (more computation intense, less error prone)
    DW1000Time round1 = (timePollAckReceived - timePollSent).wrap();
    DW1000Time reply1 = (timePollAckSent - timePollReceived).wrap();
    DW1000Time round2 = (timeRangeReceived - timePollAckSent).wrap();
    DW1000Time reply2 = (timeRangeSent - timePollAckReceived).wrap();
    DW1000Time tof = (round1 * round2 - reply1 * reply2)
                      / (round1 + round2 + reply1 + reply2);
    distance[idx_anchor] = tof.getAsMeters();
}

/********
 * Main *
 ********/
void setup() {
  setupI2C();
  setupDW1000();
#if DEBUG
  Serial.begin(115200);
#endif // DEBUG
  PRINTLN(F("Setup finished"));
  PRINTLN(F("=============="));
}

void loop() {
  curMillis = millis();
  if (state == STATE_PONG
      && lastSent && curMillis - lastSent > PONG_TIMEOUT_MS) {
    PRINTLN(F("PONG timeout"));
    if (num_anchors < 3) {
      PRINTLN(F("  Not enough anchors scanned. Return to IDLE"));
      updateState(STATE_IDLE);
      return;
    } else {
      PRINTLN(F("  Starting ROUNDROBIN..."));
      idx_anchor = 0;
      updateState(STATE_ROUNDROBIN);
      return;
    }
  }
  if (state == STATE_POLLACK && lastSent
      && curMillis - lastSent > POLLACK_TIMEOUT_MS) {
    PRINTLN(F("POLLACK timeout"));
    PRINTLN(F("  Return to ROUNDROBIN"));
    idx_anchor++;
    updateState(STATE_ROUNDROBIN);
    return;
  }
  if (state == STATE_RANGEREPORT && lastSent
      && curMillis - lastSent > RANGEREPORT_TIMEOUT_MS) {
    PRINTLN(F("RANGEREPORT timeout"));
    PRINTLN(F("  Return to ROUNDROBIN"));
    idx_anchor++;
    updateState(STATE_ROUNDROBIN);
    return;
  }

  if (state == STATE_SCAN) {
    PRINTLN(F("State: SCAN"));
    PRINTLN(F("  Initializing arrays..."));
    for (idx_anchor = 0; idx_anchor < NUM_ANCHORS; idx_anchor++) {
      anchorId[idx_anchor] = ID_NONE;
      distance[idx_anchor] = 0;
    }
    idx_anchor = 0;
    num_anchors = 0;
    PRINTLN(F("  Sending PING..."));
    transmitPing();
    updateState(STATE_PONG);
    return;
  }

  if (state == STATE_ROUNDROBIN) {
    PRINTLN(F("State: ROUNDROBIN"));
    PRINT(F("  idx_anchor: ")); PRINTLN(idx_anchor);
    if (idx_anchor < num_anchors) {
    PRINTLN(F("  Sending POLL..."));
      transmitPoll();
      updateState(STATE_POLLACK);
    } else {
      PRINTLN(F("  Ranging all anchors. Return to IDLE"));
      updateState(STATE_IDLE);
    }
    return;
  }

  if (sentFrame) {
    PRINTLN(F("Sent something"));
    sentFrame = false;
    if (txBuffer[0] == FTYPE_PING) {
      PRINTLN(F("  PING sent"));
      lastSent = millis();
    }
    if (txBuffer[0] == FTYPE_POLL) {
      PRINTLN(F("  POLL sent. Getting timestamp..."));
      DW1000.getTransmitTimestamp(timePollSent);
      lastSent = millis();
    }
    if (txBuffer[0] == FTYPE_RANGE) {
      PRINTLN(F("  RANGE sent. Getting timestamp..."));
      DW1000.getTransmitTimestamp(timeRangeSent);
      lastSent = millis();
    }
  }

  if (receivedFrame) {
    PRINTLN(F("Received something"));
    receivedFrame = false;
    DW1000.getData(rxBuffer, FRAME_LEN);

    if (state == STATE_PONG) {
      PRINTLN(F("  State: PONG"));
      if (rxBuffer[0] != FTYPE_PONG) {
        PRINTLN(F("    Not PONG"));
        return;
      }
      if (!DOES_DST_MATCH(rxBuffer, tagId, ADDR_SIZE)) {
        PRINTLN(F("    Not for me"));
        return;
      }
      #warning "This may store anchors with the same ID"
      memcpy(&anchorId[idx_anchor], rxBuffer + 1, ADDR_SIZE);
      PRINT(F("    Anchor")); PRINT(idx_anchor); PRINT(F("(")); PRINT(anchorId[idx_anchor]); PRINTLN(F(") found"));
      num_anchors++;
      idx_anchor++;
      return;
    }

    if (state == STATE_POLLACK) {
      PRINTLN(F("  State: POLLACK"));
      if (rxBuffer[0] != FTYPE_POLLACK) {
        PRINTLN(F("    Not POLLACK"));
        return;
      }
      if (!DOES_SRC_MATCH(rxBuffer, anchorId[idx_anchor], ADDR_SIZE)) {
        PRINTLN(F("    Not from counter part"));
        return;
      }
      if (!DOES_DST_MATCH(rxBuffer, tagId, ADDR_SIZE)) {
        PRINTLN(F("    Not for me"));
        return;
      }
      PRINTLN(F("    Received POLLACK. Getting timestamp..."));
      DW1000.getReceiveTimestamp(timePollAckReceived);
      PRINTLN(F("    Sending RANGE..."));
      transmitRange();
      updateState(STATE_RANGEREPORT);
      return;
    }

    if (state == STATE_RANGEREPORT) {
      PRINTLN(F("  State: RANGEREPORT"));
      if (rxBuffer[0] != FTYPE_RANGEREPORT) {
        PRINTLN(F("    Not RANGEREPORT"));
        return;
      }
      if (!DOES_SRC_MATCH(rxBuffer, anchorId[idx_anchor], ADDR_SIZE)) {
        PRINTLN(F("    Not from countere part"));
        return;
      }
      if (!DOES_DST_MATCH(rxBuffer, tagId, ADDR_SIZE)) {
        PRINTLN(F("    Not for me"));
        return;
      }
      PRINTLN(F("    Received RANGEREPORT. Getting timestamps..."));
      timePollReceived.setTimestamp(rxBuffer + 5);
      timePollAckSent.setTimestamp(rxBuffer + 10);
      timeRangeReceived.setTimestamp(rxBuffer + 15);
      PRINTLN(F("    Calculating range..."));
      calculateRange();
      PRINT(F("    ")); PRINTLN(distance[idx_anchor]);
      idx_anchor++;
      updateState(STATE_ROUNDROBIN);
      return;
    }
  }
}
