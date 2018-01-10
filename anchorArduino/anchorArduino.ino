#define DEBUG true

#include <SPI.h>

#include <DW1000.h>

#include "debug.h"
#include "def.h"
#include "arduino.h"

#define PIN_IRQ  2
#define PIN_RST  9
#define PIN_SS  SS

/* Edit anchorId */
// Each anchor must have a unique anchor ID (do not use ID 0)
const uint16_t anchorId = 1;
// UWB anchors and tags must have the same network ID
const uint16_t networkId = 10;
// Sender of the last received frame in the S/W buffer
uint16_t sender;
// Counter part tag during two way ranging (PING, PONNG, RANGE, RANGEREPORT)
uint16_t tagCounterPart = ID_NONE;

// Current state of a UWB anchor state machine
char state = STATE_IDLE;

// UWB anchors and tags must have the same replay_delay
DW1000Time reply_delay = DW1000Time(REPLY_DELAY_MS, DW1000Time::MILLISECONDS);
DW1000Time timePollReceived;
DW1000Time timePollAckSent;
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

void noteActivity() {
  lastActivity = millis();
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
  DW1000.setDeviceAddress(anchorId);
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

void transmitPong() {
  prepareTx();
  txBuffer[0] = FTYPE_PONG;
  SET_SRC(txBuffer, anchorId, ADDR_SIZE);
  SET_DST(txBuffer, sender, ADDR_SIZE);
  startTx();
}

void transmitPollAck() {
  prepareTx();
  txBuffer[0] = FTYPE_POLLACK;
  SET_SRC(txBuffer, anchorId, ADDR_SIZE);
  SET_DST(txBuffer, tagCounterPart, ADDR_SIZE);
  DW1000.setDelay(reply_delay);
  startTx();
}

void transmitRangeReport() {
  prepareTx();
  txBuffer[0] = FTYPE_RANGEREPORT;
  SET_SRC(txBuffer, anchorId, ADDR_SIZE);
  SET_DST(txBuffer, tagCounterPart, ADDR_SIZE);
  timePollReceived.getTimestamp(txBuffer + 5);
  timePollAckSent.getTimestamp(txBuffer + 10);
  timeRangeReceived.getTimestamp(txBuffer + 15);
  startTx();
}

/********
 * Main *
 ********/
void setup() {
  setupDW1000();
#if DEBUG
  Serial.begin(115200);
#endif // DEBUG
  PRINTLN(F("Setup finished"));
  PRINTLN(F("=============="));
  randomSeed(analogRead(0));
}

void loop() {
  curMillis = millis();
  // Safety watchdog to avoid stuck in PENDING_PONG state
  // Sometimes SPI tx interrupt may not be captured by Arduino
  if (state == STATE_PENDING_PONG
      && curMillis - lastStateChange > PENDING_PONG_TIMEOUT_MS) {
    PRINTLN(F("Seems Pending Pong lost. Return to IDLE"));
    updateState(STATE_IDLE);
  }
  // Safety watchdog to avoid stuck in RANGE state
  // 1. If SPI tx interrupt is captured (confirmed that POLLACK is sent)
  // 2. If SPI tx interrupt is not captured for some reasons
  if (state == STATE_RANGE
      && ((lastSent && curMillis - lastSent > RANGE_TIMEOUT_MS)
          || curMillis - lastStateChange > 2 * RANGE_TIMEOUT_MS)) {
    /*
     * Check RANGE message timeout when state is waiting for RANGE message
     */
    PRINTLN(F("RANGE timeout. Return to IDLE"));
    updateState(STATE_IDLE);
    return;
  }
  // Arduino didn't capture SIP tx/rx interrupts for more than RESET_TIMEOUT_MS
  if (!sentFrame && !receivedFrame && curMillis - lastActivity > RESET_TIMEOUT_MS) {
    PRINTLN(F("Seems transceiver not working. Re-init it."));
    initDW1000Receiver();
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

    if (state == STATE_PENDING_PONG && txBuffer[0] == FTYPE_PONG) {
      PRINTLN(F("  Pending PONG sent. Return to IDLE"));
      updateState(STATE_IDLE);
      return;
    }

    if (txBuffer[0] == FTYPE_POLLACK) {
      PRINTLN(F("  POLLACK sent. Getting timestamp..."));
      DW1000.getTransmitTimestamp(timePollAckSent);
    }

    if (txBuffer[0] == FTYPE_RANGEREPORT) {
      PRINTLN(F("  RANGEREPORT sent"));
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
    GET_SRC(rxBuffer, sender, ADDR_SIZE);

    if (state == STATE_IDLE) {
      PRINTLN(F("  State: IDLE"));
      if (rxBuffer[0] == FTYPE_PING) {
        PRINTLN(F("    Received PING. Reply with PONG"));
      /*
       * Simple random backoff [0, PONG_TIMEOUT_MS - 10) milliseconds
       */
        int d = random(0, PONG_TIMEOUT_MS - 10);
        PRINT(F("    PONG delayed ")); PRINT(d); PRINTLN(F(" ms"));
        delay(d);
        transmitPong();
        updateState(STATE_PENDING_PONG);
        return;
      }
      if (rxBuffer[0] == FTYPE_POLL) {
        PRINTLN(F("    Received POLL"));
        if (!DOES_DST_MATCH(rxBuffer, anchorId, ADDR_SIZE)) {
          PRINTLN(F("      Not for me"));
          return;
        }
        PRINTLN(F("      Reply with POLLACK"));
        DW1000.getReceiveTimestamp(timePollReceived);
        tagCounterPart = sender;
        transmitPollAck();
        updateState(STATE_RANGE);
        return;
      }
    }

    if (state == STATE_PENDING_PONG) {
      PRINTLN(F("  State: PENDING_PONG"));
      PRINTLN(F("    Ignore all received frames"));
      /*
       * PONG message is pending to be transmitted
       * Anchor should ignore all other messages
       */
      return;
    }

    if (state == STATE_RANGE) {
      PRINTLN(F("  State: RANGE"));
      if (rxBuffer[0] != FTYPE_RANGE) {
        PRINTLN(F("    Not RANGE"));
        return;
      }
      if (!DOES_SRC_MATCH(rxBuffer, tagCounterPart, ADDR_SIZE)) {
        PRINTLN(F("    Not from counter part"));
        return;
      }
      PRINTLN(F("    Sending RANGEREPORT..."));
      DW1000.getReceiveTimestamp(timeRangeReceived);
      transmitRangeReport();
      updateState(STATE_IDLE);
      return;
    }
  }
}
