#include <SPI.h>
#include <DW1000.h>

#include "arduino-common.h"

byte expectedMsg;

byte txBuffer[LEN_DATA];
byte rxBuffer[LEN_DATA];

uint32_t lastBeacon;

const uint16_t networkId = 10;
const uint16_t anchorId = MASK_ANCHOR | MASK_RIGHT | 0;
uint16_t tagId = TAG_NONE;

volatile boolean sentFrame = false;
volatile boolean receivedFrame = false;

DW1000Time timePollSent;
DW1000Time timePollReceived;
DW1000Time timePollAckSent;
DW1000Time timePollAckReceived;
DW1000Time timeRangeSent;
DW1000Time timeRangeReceived;

void handleSent() {
  sentFrame = true;
}

void handleReceived() {
  receivedFrame = true;
}

void initReceiver() {
  DW1000.newReceive();
  DW1000.setDefaults();
  DW1000.receivePermanently(true);
  DW1000.startReceive();
}

void transmitBeacon() {
  DW1000.newTransmit();
  DW1000.setDefaults();
  SET_TYPE(BEACON);
  SET_SOURCE(anchorId);
  SET_DEST(tagId);
  DW1000Time delay = DW1000Time(random(0, BEACON_JITTER_MS),
                                DW1000Time::MILLISECONDS);
  DW1000.setDelay(delay);
  DW1000.setData(txBuffer, LEN_DATA);
  DW1000.startTransmit();
  UPDATE_LAST_BEACON;
}

void transmitPollAck() {
  DW1000.newTransmit();
  DW1000.setDefaults();
  SET_TYPE(POLL_ACK);
  SET_SOURCE(anchorId);
  SET_DEST(tagId);
  // delay the same amount as ranging tag
  DW1000Time delay = DW1000Time(REPLY_DELAY_MS, DW1000Time::MILLISECONDS);
  DW1000.setDelay(delay);
  DW1000.setData(txBuffer, LEN_DATA);
  DW1000.startTransmit();
}

void transmitRangeReport() {
  DW1000.newTransmit();
  DW1000.setDefaults();
  SET_TYPE(RANGE_REPORT);
  SET_SOURCE(anchorId);
  SET_DEST(tagId);
  timeRangeReceived.getTimestamp(txBuffer + 5);
  DW1000.setData(txBuffer, LEN_DATA);
  DW1000.startTransmit();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  DW1000.begin(PIN_IRQ, PIN_RST);
  DW1000.select(PIN_SS);
  DW1000.newConfiguration();
  DW1000.setDefaults();
  DW1000.setNetworkId(networkId);
  DW1000.setDeviceAddress(anchorId);
  DW1000.enableMode(DW1000.MODE_LONGDATA_RANGE_LOWPOWER);
  DW1000.commitConfiguration();

  DW1000.attachSentHandler(handleSent);
  DW1000.attachReceivedHandler(handleReceived);

  initReceiver();

  UPDATE_LAST_BEACON;
}

void loop() {
  uint32_t curMillis = millis();
  if (curMillis - lastBeacon > BEACON_PERIOD_MS) {
    transmitBeacon();
  }
  if (curMillis - timePollAckSent.getAsMicroSeconds() * 1000
      > TIMEOUT_PERIOD_MS) {
    expectedMsg = POLL;
    tagId = TAG_NONE;
  }
  if (sentFrame) {
    sentFrame = false;
    if (GET_TYPE(txBuffer) == POLL_ACK) {
      DW1000.getTransmitTimestamp(timePollAckSent);
    }
  }
  if (receivedFrame) {
    receivedFrame = false;
    byte msg = GET_TYPE(rxBuffer);
    if (msg != expectedMsg || !DOES_MATCH_DEST(anchorId)) {
      return;
    }
    if (msg == POLL) {
      DW1000.getReceiveTimestamp(timePollReceived);
      expectedMsg = RANGE;
      GET_SOURCE(tagId);
      transmitPollAck();
    } else if (msg == RANGE && DOES_MATCH_SOURCE(tagId)) {
      DW1000.getReceiveTimestamp(timeRangeReceived);
      expectedMsg = POLL;
      transmitRangeReport();
    }
  }
}
