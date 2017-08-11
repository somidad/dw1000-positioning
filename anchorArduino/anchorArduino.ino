#include <SPI.h>

#include <DW1000.h>

#include "def.h"
#include "dwm1000.h"

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

byte txBuffer[FRAME_LEN];
byte rxBuffer[FRAME_LEN];

boolean sentFrame = false;
boolean receivedFrame = false;

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
  DW1000.setDeviceAddress(anchorId);
  DW1000.enableMode(DW1000.MODE_LONGDATA_RANGE_LOWPOWER);
  DW1000.commitConfiguration();

  DW1000.attachSentHandler(spiSendEvent);
  DW1000.attachReceivedHandler(spiReceiveEvent);

  initDW1000Receiver();
}

void transmitPong() {
  DW1000.newTransmit();
  DW1000.setDefaults();
  txBuffer[0] = FTYPE_PONG;
  memcpy(txBuffer + 1, &anchorId, ADDR_SIZE);
  memcpy(txBuffer + 3, &sender, ADDR_SIZE);
  /*
   * Simple random backoff [0, PONG_TIMEOUT_MS - 10) milliseconds
   */
  DW1000Time random_delay = DW1000Time(random(0, PONG_TIMEOUT_MS - 10),
    DW1000Time::MILLISECONDS);
  DW1000.setDelay(random_delay);
  DW1000.setData(txBuffer, FRAME_LEN);
  DW1000.startTransmit();
}

void transmitPollAck() {
  DW1000.newTransmit();
  DW1000.setDefaults();
  txBuffer[0] = FTYPE_POLLACK;
  memcpy(txBuffer + 1, &anchorId, ADDR_SIZE);
  memcpy(txBuffer + 3, &tagCounterPart, ADDR_SIZE);
  DW1000.setDelay(reply_delay);
  DW1000.setData(txBuffer, FRAME_LEN);
  DW1000.startTransmit();
}

void transmitRangeReport() {
  DW1000.newTransmit();
  DW1000.setDefaults();
  txBuffer[0] = FTYPE_RANGEREPORT;
  memcpy(txBuffer + 1, &anchorId, ADDR_SIZE);
  memcpy(txBuffer + 3, &tagCounterPart, ADDR_SIZE);
  timePollReceived.getTimestamp(txBuffer + 5);
  timePollAckSent.getTimestamp(txBuffer + 10);
  timeRangeReceived.getTimestamp(txBuffer + 15);
  DW1000.setData(txBuffer, FRAME_LEN);
  DW1000.startTransmit();
}

/********
 * Main *
 ********/
void setup() {
  setupDW1000();
}

void loop() {
  curMillis = millis();
  if (state == STATE_RANGE && curMillis - lastSent > RANGE_TIMEOUT_MS) {
    /*
     * Check RANGE message timeout when state is waiting for RANGE message
     */
    state = STATE_IDLE;
    return;
  }

  if (sentFrame) {
    sentFrame = false;

    if (state == STATE_PENDING_PONG && txBuffer[0] == FTYPE_PONG) {
      state = STATE_IDLE;
      return;
    }

    if (txBuffer[0] == FTYPE_POLLACK) {
      DW1000.getTransmitTimestamp(timePollAckSent);
    }
  }

  if (receivedFrame) {
    receivedFrame = false;
    DW1000.getData(rxBuffer, FRAME_LEN);
    memcpy(&sender, rxBuffer + 1, ADDR_SIZE);

    if (state == STATE_IDLE) {
      if (rxBuffer[0] == FTYPE_PING) {
        transmitPong();
        state = STATE_PENDING_PONG;
        return;
      }
      if (rxBuffer[0] == FTYPE_POLL) {
        if (memcmp(rxBuffer + 3, &anchorId, ADDR_SIZE)) {
          return;
        }
        DW1000.getReceiveTimestamp(timePollReceived);
        tagCounterPart = sender;
        transmitPollAck();
        state = STATE_RANGE;
        return;
      }
    }

    if (state == STATE_PENDING_PONG) {
      /*
       * PONG message is pending to be transmitted
       * Anchor should ignore all other messages
       */
      return;
    }

    if (state == STATE_RANGE) {
      if (rxBuffer[0] != FTYPE_RANGE) {
        return;
      }
      if (memcmp(rxBuffer + 1, &tagCounterPart, ADDR_SIZE)) {
        return;
      }
      DW1000.getReceiveTimestamp(timeRangeReceived);
      transmitRangeReport();
      state = STATE_IDLE;
      return;
    }
  }
}
