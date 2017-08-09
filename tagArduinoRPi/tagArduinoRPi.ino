#include <Wire.h>

#define CMD_NONE      0
#define CMD_SCAN      1
#define CMD_TYPE_NONE 2
#define CMD_TYPE_ID   3
#define CMD_TYPE_DIST 4

#define STATE_IDLE 0
#define STATE_SCAN 1

#define TYPE_NONE 0
#define TYPE_ID   1
#define TYPE_DIST 2

#define ID_NONE 0

#define NUM_ANCHORS 5

char cmd;
char state;
char type;
unsigned int anchorId[NUM_ANCHORS] = {ID_NONE, ID_NONE, ID_NONE, ID_NONE, ID_NONE};
float distance[NUM_ANCHORS] ={0, 0, 0, 0, 0};

/***********************************************
 * I2C Raspberry Pi (master) - Arduino (slave) *
 ***********************************************/
void receiveEvent(int bytes) {
  cmd = Wire.read();
  if (cmd == CMD_SCAN) {
    state = STATE_SCAN;
    return;
  }
  if (cmd == CMD_TYPE_NONE) {
    type = TYPE_NONE;
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

void requestEvent() {
  if (state == STATE_SCAN || type == TYPE_NONE) {
    Wire.write(0);
    return;
  }
  if (type == TYPE_ID) {
    Wire.write(anchorId, 2 * NUM_ANCHORS);
    return;
  }
  if (type == TYPE_DIST) {
    Wire.write(distance, 4 * NUM_ANCHORS);
    return;
  }
}

void setupI2C() {
  // 7-bit addressing
  // ref: table 3, page 17, http://www.nxp.com/docs/en/user-guide/UM10204.pdf
  Wire.begin(0x04);
  Wire.onRequest(requestEvent);
  Wire.onReceive(receiveEvent);
  type = TYPE_NONE;
}

/*************************************
 * Arduino (master) - DW1000 (slave) *
 *************************************/

void setupDW1000() {
}

/*************
 * Main *
 *************/
void setup() {
  state = STATE_IDLE;
  setupI2C();
  setupSPI();
}

void loop() {
  if (state = STATE_SCAN) {
    if (/* scan done?*/) {
      state = STATE_IDLE;
    }
  }
}
