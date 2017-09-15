#include <SPI.h>

#include <DW1000.h>

#include "def.h"
#include "dwm1000.h"

#define DEBUG true

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

void transmitPong() {
  DW1000.newTransmit();
  DW1000.setDefaults();
  txBuffer[0] = FTYPE_PONG;
  SET_SRC(txBuffer, anchorId, ADDR_SIZE);
  SET_DST(txBuffer, sender, ADDR_SIZE);
  DW1000.setData(txBuffer, FRAME_LEN);
  DW1000.startTransmit();
}

void transmitPollAck() {
  DW1000.newTransmit();
  DW1000.setDefaults();
  txBuffer[0] = FTYPE_POLLACK;
  SET_SRC(txBuffer, anchorId, ADDR_SIZE);
  SET_DST(txBuffer, tagCounterPart, ADDR_SIZE);
  DW1000.setDelay(reply_delay);
  DW1000.setData(txBuffer, FRAME_LEN);
  DW1000.startTransmit();
  lastSent = 0;
}

void transmitRangeReport() {
  DW1000.newTransmit();
  DW1000.setDefaults();
  txBuffer[0] = FTYPE_RANGEREPORT;
  SET_SRC(txBuffer, anchorId, ADDR_SIZE);
  SET_DST(txBuffer, tagCounterPart, ADDR_SIZE);
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
#if DEBUG
  Serial.begin(115200);
  Serial.println(F("Setup finished"));
  Serial.println(F("=============="));
#endif /* DEBUG */
  randomSeed(analogRead(0));
}

void loop() {
  curMillis = millis();
  if (state == STATE_RANGE && lastSent && curMillis - lastSent > RANGE_TIMEOUT_MS) {
    /*
     * Check RANGE message timeout when state is waiting for RANGE message
     */
#if DEBUG
    Serial.println(F("RANGE timeout. Return to IDLE"));
#endif /* DEBUG */
    state = STATE_IDLE;
    return;
  }
  if (!sentFrame && !receivedFrame && (curMillis - lastActivity > RESET_TIMEOUT_MS)) {
    Serial.println(F("Seems transceiver not working. Re-init it."));
    initDW1000Receiver();
    return;
  }

  if (sentFrame) {
#if DEBUG
    Serial.println(F("Sent something"));
#endif /* DEBUG */
    sentFrame = false;

    if (state == STATE_PENDING_PONG && txBuffer[0] == FTYPE_PONG) {
#if DEBUG
      Serial.println(F("  Pending PONG sent. Return to IDLE"));
#endif /* DEBUG */
      state = STATE_IDLE;
      noteActivity();
      return;
    }

    if (txBuffer[0] == FTYPE_POLLACK) {
#if DEBUG
      Serial.println(F("  POLLACK sent. Getting timestamp..."));
#endif /* DEBUG */
      DW1000.getTransmitTimestamp(timePollAckSent);
      noteActivity();
    }

    if (txBuffer[0] == FTYPE_RANGEREPORT) {
#if DEBUG
      Serial.println(F("  RANGEREPORT sent"));
#endif /* DEBUG */
      noteActivity();
    }
  }

  if (receivedFrame) {
#if DEBUG
    Serial.println(F("Received something"));
#endif /* DEBUG */
    receivedFrame = false;
    DW1000.getData(rxBuffer, FRAME_LEN);
    GET_SRC(rxBuffer, sender, ADDR_SIZE);

    if (state == STATE_IDLE) {
#if DEBUG
      Serial.println(F("  State: IDLE"));
#endif /* DEBUG */
      if (rxBuffer[0] == FTYPE_PING) {
#if DEBUG
        Serial.println(F("    Received PING. Reply with PONG"));
#endif /* DEBUG */
      /*
       * Simple random backoff [0, PONG_TIMEOUT_MS - 10) milliseconds
       */
        int d = random(0, PONG_TIMEOUT_MS - 10);
#if DEBUG
        Serial.print(F("    PONG delayed "));
        Serial.print(d);
        Serial.println(F(" ms"));
#endif /* DEBUG */
        delay(d);
        transmitPong();
        state = STATE_PENDING_PONG;
        noteActivity();
        return;
      }
      if (rxBuffer[0] == FTYPE_POLL) {
#if DEBUG
        Serial.println(F("    Received POLL"));
#endif /* DEBUG */
        if (!DOES_DST_MATCH(rxBuffer, anchorId, ADDR_SIZE)) {
#if DEBUG
          Serial.println(F("      Not for me"));
#endif /* DEBUG */
          return;
        }
#if DEBUG
        Serial.println(F("      Reply with POLLACK"));
#endif /* DEBUG */
        DW1000.getReceiveTimestamp(timePollReceived);
        tagCounterPart = sender;
        transmitPollAck();
        state = STATE_RANGE;
        noteActivity();
        return;
      }
    }

    if (state == STATE_PENDING_PONG) {
#if DEBUG
      Serial.println(F("  State: PENDING_PONG"));
      Serial.println(F("    Ignore all received frames"));
#endif /* DEBUG */
      /*
       * PONG message is pending to be transmitted
       * Anchor should ignore all other messages
       */
      noteActivity();
      return;
    }

    if (state == STATE_RANGE) {
#if DEBUG
      Serial.println(F("  State: RANGE"));
#endif /* DEBUG */
      if (rxBuffer[0] != FTYPE_RANGE) {
#if DEBUG
        Serial.println(F("    Not RANGE"));
#endif /* DEBUG */
        return;
      }
      if (!DOES_SRC_MATCH(rxBuffer, tagCounterPart, ADDR_SIZE)) {
#if DEBUG
        Serial.println(F("    Not from counter part"));
#endif /* DEBUG */
        return;
      }
#if DEBUG
      Serial.println(F("    Sending RANGEREPORT..."));
#endif
      DW1000.getReceiveTimestamp(timeRangeReceived);
      transmitRangeReport();
      state = STATE_IDLE;
      noteActivity();
      return;
    }
  }
}
