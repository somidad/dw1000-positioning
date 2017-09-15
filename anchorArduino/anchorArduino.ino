#include <SPI.h>

#include <DW1000.h>

#include "def.h"
#include "dwm1000.h"

#define DEBUG true
#if DEBUG
  #define PRINT(x)   Serial.print(x)
  #define PRINTLN(x) Serial.println(x)
#else // DEBUG
  #define PRINT(x)
  #define PRINTLN(x)
#endif // DEBUG

#define PIN_IRQ  2
#define PIN_RST  9
#define PIN_SS  SS

/* Edit anchorId */
const uint16_t anchorId = 1;
const uint16_t networkId = 10;
uint16_t sender;
uint16_t tagCounterPart = ID_NONE;

char state = STATE_IDLE;

DW1000Time reply_delay = DW1000Time(REPLY_DELAY_MS, DW1000Time::MILLISECONDS);
DW1000Time timePollReceived;
DW1000Time timePollAckSent;
DW1000Time timeRangeReceived;

unsigned long curMillis;
unsigned long lastSent;
unsigned long lastActivity;

byte txBuffer[FRAME_LEN];
byte rxBuffer[FRAME_LEN];

boolean sentFrame = false;
boolean receivedFrame = false;

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

void clearLastSent() {
  lastSent = 0;
}

void startTx() {
  DW1000.setData(txBuffer, FRAME_LEN);
  DW1000.startTransmit();
  clearLastSent();
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
  if (state == STATE_RANGE && lastSent && curMillis - lastSent > RANGE_TIMEOUT_MS) {
    /*
     * Check RANGE message timeout when state is waiting for RANGE message
     */
    PRINTLN(F("RANGE timeout. Return to IDLE"));
    state = STATE_IDLE;
    return;
  }
  if (!sentFrame && !receivedFrame && (curMillis - lastActivity > RESET_TIMEOUT_MS)) {
    PRINTLN(F("Seems transceiver not working. Re-init it."));
    initDW1000Receiver();
    return;
  }

  if (sentFrame) {
    PRINTLN(F("Sent something"));
    sentFrame = false;

    if (state == STATE_PENDING_PONG && txBuffer[0] == FTYPE_PONG) {
      PRINTLN(F("  Pending PONG sent. Return to IDLE"));
      state = STATE_IDLE;
      lastSent = millis();
      noteActivity();
      return;
    }

    if (txBuffer[0] == FTYPE_POLLACK) {
      PRINTLN(F("  POLLACK sent. Getting timestamp..."));
      DW1000.getTransmitTimestamp(timePollAckSent);
      lastSent = millis();
      noteActivity();
    }

    if (txBuffer[0] == FTYPE_RANGEREPORT) {
      lastSent = millis();
      noteActivity();
      PRINTLN(F("  RANGEREPORT sent"));
    }
  }

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
        state = STATE_PENDING_PONG;
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
        state = STATE_RANGE;
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
      state = STATE_IDLE;
      return;
    }
  }
}
