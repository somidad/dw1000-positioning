#define DEBUG true

#include <Wire.h>
#include <SPI.h>

#include <DW1000.h>

#include "debug.h"
#include "def.h"
#include "i2c.h"
#include "arduino.h"

#define PIN_IRQ  2
#define PIN_RST  9
#define PIN_SS  SS

/* Edit tagId */
// Each tag must have a unique tag ID (do not use ID 0)
// DO NOT REMOVE FLAT_TAG
const uint16_t tagId = FLAG_TAG | 1;
// UWB anchors and tags must have the same network ID
const uint16_t networkId = 10;

// Command sent by Raspberry Pi via I2C interface
volatile char cmd = CMD_NONE;
// Current state of a UWB anchor state machine
volatile char state = STATE_IDLE;
// Type of data that Raspberry Pi wants (anchor ID, distance measurements)
volatile char type = TYPE_NONE;

// The number of observed anchors
unsigned char num_anchors = 0;
// Index of the current anchor in two way ranging
unsigned char idx_anchor = 0;
// Array of anchors' IDs
uint16_t anchorId[NUM_ANCHORS] = {ID_NONE, ID_NONE, ID_NONE, ID_NONE, ID_NONE};
// Array of distance measurements of the corresponding anchors
float distance[NUM_ANCHORS] = {0, 0, 0, 0, 0};

// UWB anchors and tags must have the same replay_delay
DW1000Time reply_delay = DW1000Time(REPLY_DELAY_MS, DW1000Time::MILLISECONDS);
DW1000Time timePollSent;
DW1000Time timePollReceived;
DW1000Time timePollAckSent;
DW1000Time timePollAckReceived;
DW1000Time timeRangeSent;
DW1000Time timeRangeReceived;

// Last time that loop() is called
unsigned long curMillis;
// Last time that a frame is PUSHED INTO THE AIR FROM THE S/W BUFFER
unsigned long lastSent;
// Last time that a UWB device sends or receives a frame
// i.e., meaningful DWM1000 activity
unsigned long lastActivity;
unsigned long lastStateChange;

byte txBuffer[FRAME_LEN];
byte rxBuffer[FRAME_LEN];

// Set to true if a frame is pushed into the air and SPI tx interrupt is triggered
boolean sentFrame = false;
// Set to true if a frame is received and SPI rx interrupt is triggered
// Not yet stored into the S/W buffer
boolean receivedFrame = false;

void updateState(int nextState) {
  state = nextState;
  lastStateChange = millis();
}

void updateRoundRobin() {
  idx_anchor++;
  updateState(STATE_ROUNDROBIN);
}

void noteActivity() {
  lastActivity = millis();
}

/***********************************************
 * I2C Raspberry Pi (master) - Arduino (slave) *
 ***********************************************/
// Raspberry Pi first writes and Arduino reads data
void i2cReceiveEvent(int bytes) {
  if (!bytes) {
    return;
  }
  // Although Raspberry PI sends 1-byte long data via I2C interface,
  // It is safe to read bytes as many as Arduino can until there is no data remained
  while (Wire.available()) {
    cmd = Wire.read();
  }
  if (cmd == CMD_SCAN && state == STATE_IDLE) {
    PRINTLN(F("Transitting to SCAN state..."));
    updateState(STATE_SCAN);
    return;
  }
  // Raspberry Pi requests data representing whether data is ready
  if (cmd == CMD_DATA_READY) {
    type = TYPE_DATA_READY;
    return;
  }
  // Raspberry Pi requests data representing anchors' IDs
  if (cmd == CMD_TYPE_ID) {
    type = TYPE_ID;
    return;
  }
  // Raspberry Pi requests data representing distance measurements
  if (cmd == CMD_TYPE_DIST) {
    type = TYPE_DIST;
    return;
  }
}

// And then Arduino writes and Raspberry Pi reads data
void i2cRequestEvent() {
  if (state != STATE_IDLE || type == TYPE_NONE) {
    Wire.write(I2C_NODATA);
    return;
  }
  if (type == TYPE_DATA_READY) {
    Wire.write(I2C_DATARD);
    return;
  }
  // HACK: Regard array of anchors' IDs and distance measurements as a long binay data
  // Note that Arduino uses little endian
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
  noteActivity();
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
    // See APS013: The Implementation of Two-Way Ranging with he DW1000
    // https://decawave.com/sites/default/files/aps013_dw1000_and_two_way_ranging_v2.2.pdf pp. 4
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
  // Safety watchdog to avoid stuck in PONG state
  // 1. If SPI tx interrupt captured (confirmed that PING is broadcasted)
  // 2. If SPI tx interrupt is not captured for some reason
  if (state == STATE_PONG
      && ((lastSent && curMillis - lastSent > PONG_TIMEOUT_MS)
          || curMillis - lastStateChange > 2 * PONG_TIMEOUT_MS)) {
    PRINTLN(F("PONG timeout"));
    if (num_anchors < 3) {
      // Seems not enough anchors exist (lack of deployment, collision, delayed tx)
      PRINTLN(F("  Not enough anchors scanned. Return to IDLE"));
      updateState(STATE_IDLE);
      return;
    } else {
      // 3 or more anchors are observed
      // Proceed to two way ranging in a round robin manner
      PRINTLN(F("  Starting ROUNDROBIN..."));
      idx_anchor = 0;
      updateState(STATE_ROUNDROBIN);
      return;
    }
  }
  // Safety watchdog to avoid stuck in POLLACK state
  // 1. If SPI tx interrupt captured (confirmed that POLL is sent)
  // 2. If SPI tx interrupt is not captured for some reason
  if (state == STATE_POLLACK
      && ((lastSent && curMillis - lastSent > POLLACK_TIMEOUT_MS)
          || curMillis - lastStateChange > 2 * POLLACK_TIMEOUT_MS)) {
    PRINTLN(F("POLLACK timeout"));
    PRINTLN(F("  Return to ROUNDROBIN"));
    updateRoundRobin();
    return;
  }
  // Safety watchdog to avoid stuck in RANGEREPORT state
  // 1. If SPI tx interrupt captured (confirmed that RANGE is sent)
  // 2. If SPI tx interrupt is not captured for some reason
  if (state == STATE_RANGEREPORT
      && ((lastSent && curMillis - lastSent > RANGEREPORT_TIMEOUT_MS)
          || curMillis - lastStateChange > 2 * RANGEREPORT_TIMEOUT_MS)) {
    PRINTLN(F("RANGEREPORT timeout"));
    PRINTLN(F("  Return to ROUNDROBIN"));
    updateRoundRobin();
    return;
  }
  // Arduino didn't capture SIP tx/rx interrupts for more than RESET_TIMEOUT_MS
  if (!sentFrame && !receivedFrame && curMillis - lastActivity > RESET_TIMEOUT_MS) {
    PRINTLN(F("Seems transceiver not working. Re-init it."));
    initDW1000Receiver();
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

  // SPI tx interrupt is captured
  // Update some time variables, state
  // Extract DW1000 high-precision time value if needed
  if (sentFrame) {
    PRINTLN(F("Sent something"));
    sentFrame = false;
    noteActivity();
    lastSent = lastActivity;
    if (txBuffer[0] == FTYPE_PING) {
      PRINTLN(F("  PING sent"));
    }
    if (txBuffer[0] == FTYPE_POLL) {
      PRINTLN(F("  POLL sent. Getting timestamp..."));
      DW1000.getTransmitTimestamp(timePollSent);
    }
    if (txBuffer[0] == FTYPE_RANGE) {
      PRINTLN(F("  RANGE sent. Getting timestamp..."));
      DW1000.getTransmitTimestamp(timeRangeSent);
    }
  }

  // SPI rx interrupt is captured
  //  Update some time variables, state
  // Extract DW1000 high-precision time value if needed
  if (receivedFrame) {
    PRINTLN(F("Received something"));
    receivedFrame = false;
    noteActivity();
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
      num_anchors = min(num_anchors, 5);
      idx_anchor++;
      idx_anchor %= 5;
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
      updateRoundRobin();
      return;
    }
  }
}
