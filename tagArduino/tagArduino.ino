#include <Wire.h>
#include <SPI.h>

#include <DW1000.h>

#include "def.h"
#include "i2c.h"
#include "dwm1000.h"

#define DEBUG true

#define PIN_IRQ  2
#define PIN_RST  9
#define PIN_SS  SS

/* Edit tagId */
const uint16_t tagId = 1;
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
#if DEBUG
    Serial.println(F("Transitting to SCAN state..."));
#endif /* DEBUG */
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
  lastSent = 0;
}

void transmitPoll() {
  DW1000.newTransmit();
  DW1000.setDefaults();
  txBuffer[0] = FTYPE_POLL;
  SET_SRC(txBuffer, tagId, ADDR_SIZE);
  SET_DST(txBuffer, anchorId[idx_anchor], ADDR_SIZE);
  DW1000.setData(txBuffer, FRAME_LEN);
  DW1000.startTransmit();
  lastSent = 0;
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
  lastSent = 0;
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
#if DEBUG
  Serial.begin(115200);
  Serial.println(F("Setup finished"));
  Serial.println(F("=============="));
#endif /* DEBUG */
}

void loop() {
  curMillis = millis();
  if (state == STATE_PONG && lastSent && curMillis - lastSent > PONG_TIMEOUT_MS) {
#if DEBUG
    Serial.println(F("PONG timeout"));
#endif /* DEBUG */
    if (num_anchors < 3) {
#if DEBUG
      Serial.println(F("  Not enough anchors scanned. Return to IDLE"));
#endif /* DEBUG */
      state = STATE_IDLE;
      return;
    } else {
#if DEBUG
      Serial.println(F("  Starting ROUNDROBIN..."));
#endif /* DEBUG */
      idx_anchor = 0;
      state = STATE_ROUNDROBIN;
      return;
    }
  }
  if (state == STATE_POLLACK && lastSent && curMillis - lastSent > POLLACK_TIMEOUT_MS) {
#if DEBUG
    Serial.println(F("POLLACK timeout"));
    Serial.println(F("  Return to ROUNDROBIN"));
#endif /* DEBUG */
    idx_anchor++;
    state = STATE_ROUNDROBIN;
    return;
  }
  if (state == STATE_RANGEREPORT && lastSent && curMillis - lastSent > RANGEREPORT_TIMEOUT_MS) {
#if DEBUG
    Serial.println(F("RANGEREPORT timeout"));
    Serial.println(F("  Return to ROUNDROBIN"));
#endif /* DEBUG */
    idx_anchor++;
    state = STATE_ROUNDROBIN;
    return;
  }

  if (state == STATE_SCAN) {
#if DEBUG
    Serial.println(F("State: SCAN"));
    Serial.println(F("  Initializing arrays..."));
#endif /* DEBUG */
    for (idx_anchor = 0; idx_anchor < NUM_ANCHORS; idx_anchor++) {
      anchorId[idx_anchor] = ID_NONE;
      distance[idx_anchor] = 0;
    }
    idx_anchor = 0;
    num_anchors = 0;
#if DEBUG
    Serial.println(F("  Sending PING..."));
#endif /* DEBUG */
    transmitPing();
    state = STATE_PONG;
    return;
  }

  if (state == STATE_ROUNDROBIN) {
#if DEBUG
    Serial.println(F("State: ROUNDROBIN"));
    Serial.print(F("  idx_anchor: "));
    Serial.println(idx_anchor);
#endif /* DEBUG */
    if (idx_anchor < num_anchors) {
#if DEBUG
    Serial.println(F("  Sending POLL..."));
#endif /* DEBUG */
      transmitPoll();
      state = STATE_POLLACK;
    } else {
#if DEBUG
      Serial.println(F("  Ranging all anchors. Return to IDLE"));
#endif /* DEBUG */
      state = STATE_IDLE;
    }
    return;
  }

  if (sentFrame) {
#if DEBUG
    Serial.println(F("Sent something"));
#endif /* DEBUG */
    sentFrame = false;
    if (txBuffer[0] == FTYPE_PING) {
#if DEBUG
      Serial.println(F("  PING sent"));
#endif /* DEBUG */
      lastSent = millis();
    }
    if (txBuffer[0] == FTYPE_POLL) {
#if DEBUG
      Serial.println(F("  POLL sent. Getting timestamp..."));
#endif /* DEBUG */
      DW1000.getTransmitTimestamp(timePollSent);
      lastSent = millis();
    }
    if (txBuffer[0] == FTYPE_RANGE) {
#if DEBUG
      Serial.println(F("  RANGE sent. Getting timestamp..."));
#endif /* DEBUG */
      DW1000.getTransmitTimestamp(timeRangeSent);
      lastSent = millis();
    }
  }

  if (receivedFrame) {
#if DEBUG
    Serial.println(F("Received something"));
#endif /* DEBUG */
    receivedFrame = false;
    DW1000.getData(rxBuffer, FRAME_LEN);

    if (state == STATE_PONG) {
#if DEBUG
      Serial.println(F("  State: PONG"));
#endif /* DEBUG */
      if (rxBuffer[0] != FTYPE_PONG) {
#if DEBUG
        Serial.println(F("    Not PONG"));
#endif /* DEBUG */
        return;
      }
      if (!DOES_DST_MATCH(rxBuffer, tagId, ADDR_SIZE)) {
#if DEBUG
        Serial.println(F("    Not for me"));
#endif /* DEBUG */
        return;
      }
      #warning "This may store anchors with the same ID"
      memcpy(&anchorId[idx_anchor], rxBuffer + 1, ADDR_SIZE);
#if DEBUG
      Serial.print(F("    Anchor"));
      Serial.print(idx_anchor);
      Serial.print(F("("));
      Serial.print(anchorId[idx_anchor]);
      Serial.println(F(") found"));
#endif /* DEBUG */
      num_anchors++;
      idx_anchor++;
      return;
    }

    if (state == STATE_POLLACK) {
#if DEBUG
      Serial.println(F("  State: POLLACK"));
#endif /* DEBUG */
      if (rxBuffer[0] != FTYPE_POLLACK) {
#if DEBUG
        Serial.println(F("    Not POLLACK"));
#endif /* DEBUG */
        return;
      }
      if (!DOES_SRC_MATCH(rxBuffer, anchorId[idx_anchor], ADDR_SIZE)) {
#if DEBUG
        Serial.println(F("    Not from counter part"));
#endif /* DEBUG */
        return;
      }
      if (!DOES_DST_MATCH(rxBuffer, tagId, ADDR_SIZE)) {
#if DEBUG
        Serial.println(F("    Not for me"));
#endif /* DEBUG */
        return;
      }
#if DEBUG
      Serial.println(F("    Received POLLACK. Getting timestamp..."));
#endif /* DEBUG */
      DW1000.getReceiveTimestamp(timePollAckReceived);
#if DEBUG
      Serial.println(F("    Sending RANGE..."));
#endif /* DEBUG */
      transmitRange();
      state = STATE_RANGEREPORT;
      return;
    }

    if (state == STATE_RANGEREPORT) {
#if DEBUG
      Serial.println(F("  State: RANGEREPORT"));
#endif /* DEBUG */
      if (rxBuffer[0] != FTYPE_RANGEREPORT) {
#if DEBUG
        Serial.println(F("    Not RANGEREPORT"));
#endif /* DEBUG */
        return;
      }
      if (!DOES_SRC_MATCH(rxBuffer, anchorId[idx_anchor], ADDR_SIZE)) {
#if DEBUG
        Serial.println(F("    Not from countere part"));
#endif /* DEBUG */
        return;
      }
      if (!DOES_DST_MATCH(rxBuffer, tagId, ADDR_SIZE)) {
#if DEBUG
        Serial.println(F("    Not for me"));
#endif /* DEBUG */
        return;
      }
#if DEBUG
      Serial.println(F("    Received RANGEREPORT. Getting timestamps..."));
#endif /* DEBUG */
      timePollReceived.setTimestamp(rxBuffer + 5);
      timePollAckSent.setTimestamp(rxBuffer + 10);
      timeRangeReceived.setTimestamp(rxBuffer + 15);
#if DEBUG
      Serial.println(F("    Calculating range..."));
#endif /* DEBUG */
      calculateRange();
#if DEBUG
      Serial.print(F("    "));
      Serial.println(distance[idx_anchor]);
#endif /* DEBUG */
      idx_anchor++;
      state = STATE_ROUNDROBIN;
      return;
    }
  }
}
