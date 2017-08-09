#include <SPI.h>
#include <DW1000.h>

#include "arduino.h"
#include "common.h"

byte expectedMsg = BEACON;

byte txBuffer[LEN_DATA];
byte rxBuffer[LEN_DATA];

uint32_t lastSent;

const uint16_t networkId = 10;
const uint16_t tagId = 1;
uint16_t anchorId = TAG_NONE;

volatile boolean sentFrame = false;
volatile boolean receivedFrame = false;

DW1000Time timePollSent;
DW1000Time timePollReceived;
DW1000Time timePollAckSent;
DW1000Time timePollAckReceived;
DW1000Time timeRangeSent;
DW1000Time timeRangeReceived;
DW1000Time timeComputedRange;

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

void transmitPoll() {
  DW1000.newTransmit();
  DW1000.setDefaults();
  SET_TYPE(txBuffer, POLL);
  SET_SOURCE(txBuffer, tagId);
  SET_DEST(txBuffer, anchorId);
  DW1000.setData(txBuffer, LEN_DATA);
  DW1000.startTransmit();
  LOGTIME; LOGF("tx: POLL to "); LOGLN(anchorId);
}

void transmitRange() {
  DW1000.newTransmit();
  DW1000.setDefaults();
  SET_TYPE(txBuffer, RANGE);
  SET_SOURCE(txBuffer, tagId);
  SET_DEST(txBuffer, anchorId);
  // delay sending the message and remember expected future sent timestamp
  DW1000Time delay = DW1000Time(REPLY_DELAY_MS, DW1000Time::MILLISECONDS);
  DW1000.setDelay(delay);
  DW1000.setData(txBuffer, LEN_DATA);
  DW1000.startTransmit();
  LOGTIME; LOGF("tx: RANGE to "); LOGLN(anchorId);
}

/*
 * RANGING ALGORITHMS
 * ------------------
 * Either of the below functions can be used for range computation (see line "CHOSEN
 * RANGING ALGORITHM" in the code).
 * - Asymmetric is more computation intense but least error prone
 * - Symmetric is less computation intense but more error prone to clock drifts
 *
 * The anchors and tags of this reference example use the same reply delay times, hence
 * are capable of symmetric ranging (and of asymmetric ranging anyway).
 */

void computeRangeAsymmetric() {
    // asymmetric two-way ranging (more computation intense, less error prone)
    DW1000Time round1 = (timePollAckReceived - timePollSent).wrap();
    DW1000Time reply1 = (timePollAckSent - timePollReceived).wrap();
    DW1000Time round2 = (timeRangeReceived - timePollAckSent).wrap();
    DW1000Time reply2 = (timeRangeSent - timePollAckReceived).wrap();
    DW1000Time tof = (round1 * round2 - reply1 * reply2) / (round1 + round2 + reply1 + reply2);
    // set tof timestamp
    timeComputedRange.setTimestamp(tof);
}

void computeRangeSymmetric() {
    // symmetric two-way ranging (less computation intense, more error prone on clock drift)
    DW1000Time tof = ((timePollAckReceived - timePollSent) - (timePollAckSent - timePollReceived) +
                      (timeRangeReceived - timePollAckSent) - (timeRangeSent - timePollAckReceived)) * 0.25f;
    // set tof timestamp
    timeComputedRange.setTimestamp(tof);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  DW1000.begin(PIN_IRQ, PIN_RST);
  DW1000.select(PIN_SS);
  DW1000.newConfiguration();
  DW1000.setDefaults();
  DW1000.setNetworkId(networkId);
  DW1000.setDeviceAddress(tagId);
  DW1000.enableMode(DW1000.MODE_LONGDATA_RANGE_LOWPOWER);
  DW1000.commitConfiguration();

  DW1000.attachSentHandler(handleSent);
  DW1000.attachReceivedHandler(handleReceived);

  initReceiver();

  lastSent = 0;
}

void loop() {
  uint32_t curMillis = millis();
  if ((expectedMsg == POLL_ACK || expectedMsg == RANGE_REPORT) &&
      (curMillis - lastSent > TIMEOUT_PERIOD_MS)) {
    LOGTIME; LOGF("timeout of "); LOG(expectedMsg); LOGF(" at ");
    LOG(curMillis); LOGF(" (lastSent: "); LOG(lastSent); LOGFLN(")");
    expectedMsg = BEACON;
    anchorId = TAG_NONE;
  }
  if (sentFrame) {
    sentFrame = false;
    byte msgId = GET_TYPE(txBuffer);
    if (msgId == POLL) {
      DW1000.getTransmitTimestamp(timePollSent);
    } else if (msgId == RANGE) {
      DW1000.getTransmitTimestamp(timeRangeSent);
    }
  }
  if (receivedFrame) {
    receivedFrame = false;
    DW1000.getData(rxBuffer, LEN_DATA);
    byte msgId = GET_TYPE(rxBuffer);
    uint16_t nodeSent;
    GET_SOURCE(rxBuffer, nodeSent);
    LOGTIME; LOGF("rx: "); LOG(msgId); LOGF(" (expected: "); LOG(expectedMsg); LOGF(")");
    LOGF(" from "); LOG(nodeSent); LOGF(" to "); LOGLN(tagId);
    if ((msgId != expectedMsg || !DOES_MATCH_DEST(rxBuffer, tagId))
        && msgId != BEACON) {
      return;
    }
    if (msgId == BEACON) {
      GET_SOURCE(rxBuffer, anchorId);
      LOGTIME; LOGF("rx: BEACON from "); LOGLN(anchorId);
      /*
       * NOTE: This is an example.
       * You should probe at least 3 beacons
       * and then probe them sequentially
       */
      expectedMsg = POLL_ACK;
      transmitPoll();
      lastSent = millis();
    } else if (msgId == POLL_ACK) {
      DW1000.getReceiveTimestamp(timePollAckReceived);
      LOGTIME; LOGF("rx: POLL_ACK from "); LOGLN(anchorId);
      expectedMsg = RANGE_REPORT;
      transmitRange();
      lastSent = millis();
    } else if (msgId == RANGE_REPORT) {
      LOGTIME; LOGF("rx: RANGE_REPORT from "); LOGLN(anchorId);
      timePollReceived.setTimestamp(rxBuffer + 5);
      timePollAckSent.setTimestamp(rxBuffer + 10);
      timeRangeReceived.setTimestamp(rxBuffer + 15);
      computeRangeAsymmetric();
      float distance = timeComputedRange.getAsMeters();
      LOGTIME; LOGF("Range: "); LOG(distance); LOGFLN(" m");
      expectedMsg = POLL_ACK;
      transmitPoll();
    }
  }
}

