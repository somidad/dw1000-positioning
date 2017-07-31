#include <SPI.h>
#include <DW1000.h>

// pin config
#define PIN_RST 9
#define PIN_IRQ 2
#define PIN_SS SS
// message types
#define POLL 0
#define POLL_ACK 1
#define RANGE 2
#define RANGE_REPORT 3
#define BEACON 127
#define RANGE_FAILED 255
byte expectedMsg;

#define LEN_DATA 20
byte txBuffer[LEN_DATA];
byte rxBuffer[LEN_DATA];

#define REPLY_DELAY_MS 3
#define TIMEOUT_PERIOD_MS 100
#define BEACON_PERIOD_MS 500
#define BEACON_JITTER_MS 10
uint32_t lastBeacon;
#define UPDATE_LAST_BEACON (lastBeacon = millis())

#define TAG_NONE 0
#define MASK_ANCHOR (1 << 15)
#define MASK_RIGHT (1 << 14)
#define MASK_LEFT (1 << 14)
const uint16_t networkId = 10;
const uint16_t anchorId = MASK_ANCHOR | MASK_RIGHT | 0;
uint16_t tagId = TAG_NONE;

#define ADDR_SIZE 2
#define GET_TYPE(buffer) (buffer[0])
#define SET_TYPE(type) (txBuffer[0] = type)
#define GET_SOURCE (memcpy(&tagId, rxBuffer + 1, ADDR_SIZE))
#define SET_SOURCE (memcpy(txBuffer + 1, &anchorId, ADDR_SIZE))
#define SET_DEST (memcpy(txBuffer + 3, &tagId, ADDR_SIZE))
#define IS_MSG_FROM_TAG (!memcmp(rxBuffer + 1, &tagId, ADDR_SIZE))
#define IS_MSG_FOR_ME (!memcmp(rxBuffer + 3, &anchorId, ADDR_SIZE))

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
  SET_SOURCE;
  SET_DEST;
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
  SET_SOURCE;
  SET_DEST;
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
  SET_SOURCE;
  SET_DEST;
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
    if (msg != expectedMsg || !IS_MSG_FOR_ME) {
      return;
    }
    if (msg == POLL) {
      DW1000.getReceiveTimestamp(timePollReceived);
      expectedMsg = RANGE;
      GET_SOURCE;
      transmitPollAck();
    } else if (msg == RANGE && IS_MSG_FROM_TAG) {
      DW1000.getReceiveTimestamp(timeRangeReceived);
      expectedMsg = POLL;
      transmitRangeReport();
    }
  }
}
