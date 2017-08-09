#include <SPI.h>
#include <DW1000.h>

#include "arduino.h"
#include "common.h"

byte expectedMsg = POLL;

byte txBuffer[LEN_DATA];
byte rxBuffer[LEN_DATA];

uint32_t lastBeacon;
uint32_t lastSent;

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
  LOGFLN("Received something");
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
  SET_TYPE(txBuffer, BEACON);
  SET_SOURCE(txBuffer, anchorId);
  DW1000Time delay = DW1000Time(random(0, BEACON_JITTER_MS),
                                DW1000Time::MILLISECONDS);
  DW1000.setDelay(delay);
  DW1000.setData(txBuffer, LEN_DATA);
  DW1000.startTransmit();
  UPDATE_LAST_BEACON;
  LOGTIME; LOGFLN("tx: Beacon");
}

void transmitPollAck() {
  DW1000.newTransmit();
  DW1000.setDefaults();
  SET_TYPE(txBuffer, POLL_ACK);
  SET_SOURCE(txBuffer, anchorId);
  SET_DEST(txBuffer, tagId);
  // delay the same amount as ranging tag
  DW1000Time delay = DW1000Time(REPLY_DELAY_MS, DW1000Time::MILLISECONDS);
  DW1000.setDelay(delay);
  DW1000.setData(txBuffer, LEN_DATA);
  DW1000.startTransmit();
  LOGTIME; LOGF("tx: POLL_ACK to "); LOGLN(tagId);
}

void transmitRangeReport() {
  DW1000.newTransmit();
  DW1000.setDefaults();
  SET_TYPE(txBuffer, RANGE_REPORT);
  SET_SOURCE(txBuffer, anchorId);
  SET_DEST(txBuffer, tagId);
  timePollReceived.getTimestamp(txBuffer + 5);
  timePollAckSent.getTimestamp(txBuffer + 10);
  timeRangeReceived.getTimestamp(txBuffer + 15);
  DW1000.setData(txBuffer, LEN_DATA);
  DW1000.startTransmit();
  LOGTIME; LOGF("tx: RANGE_REPORT to "); LOGLN(tagId);
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
  if (expectedMsg == RANGE && (curMillis - lastSent > TIMEOUT_PERIOD_MS)) {
    LOGTIME; LOGF("timeout RANGE at ");  LOGLN(curMillis); LOGF(" (lastSent: ");
    LOG(lastSent); LOGFLN(")");
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
    DW1000.getData(rxBuffer, LEN_DATA);
    byte msg = GET_TYPE(rxBuffer);
    uint16_t nodeSent;
    GET_SOURCE(rxBuffer, nodeSent);
    LOGTIME; LOGF("rx: "); LOG(msg); LOGF(" (expected: "); LOG(expectedMsg); LOGF(")");
    LOGF(" from "); LOG(nodeSent); LOGF(" to "); LOGLN(anchorId);
    if (msg != expectedMsg || !DOES_MATCH_DEST(rxBuffer, anchorId)) {
      return;
    }
    if (msg == POLL) {
      DW1000.getReceiveTimestamp(timePollReceived);
      expectedMsg = RANGE;
      GET_SOURCE(rxBuffer, tagId);
      LOGTIME; LOGF("rx: POLL from "); LOGLN(tagId);
      transmitPollAck();
      lastSent = millis();
      LOGTIME; LOGF("last POLL_ACK sent: "); LOGLN(lastSent);
    } else if (msg == RANGE && DOES_MATCH_SOURCE(rxBuffer, tagId)) {
      LOGTIME; LOGF("rx: RANGE from "); LOGLN(tagId);
      DW1000.getReceiveTimestamp(timeRangeReceived);
      transmitRangeReport();
      expectedMsg = POLL;
      tagId = TAG_NONE;
    }
  }
}
