#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>

#include <wiringPi.h>
#include <wiringPiI2C.h>

#include "i2c.h"

#define NUM_ANCHORS 5

using namespace std;

void printUsage() {
}

int readMeasurement(int i2cFd, uint16_t* anchorId, float* distance, int num_anchors) {
  uint8_t data[32];
  wiringPiI2CWrite(i2cFd, CMD_DATA_READY);
  if (!wiringPiI2CRead(i2cFd)) {
    return -EBUSY;
  }
  wiringPiI2CWrite(i2cFd, CMD_TYPE_ID);
  for (int i = 0; i < 2 * NUM_ANCHORS; i++) {
    data[i] = wiringPiI2CRead(i2cFd);
  }
  wiringPiI2CWrite(i2cFd, CMD_TYPE_DIST);
  for (int i = 0; i < 4 * NUM_ANCHORS; i++) {
    data[i] = wiringPiI2CRead(i2cFd);
  }
  return 0;
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
     printUsage();
     return EINVAL;
  }

  wiringPiSetupGpio();
  int i2cFd = wiringPiI2CSetup(0x04);
  if (i2cFd < 0) {
    return EBADF;
  }

  uint8_t data[32] = {0, };
  uint16_t anchorId[NUM_ANCHORS] = {0, };
  float distance[NUM_ANCHORS] = {0, };

  if (strcmp(argv[1], "scan")) {
    wiringPiI2CWrite(i2cFd, CMD_SCAN);
  }
  if (strcmp(argv[1], "read")) {
    int ret = readMeasurement(i2cFd, anchorId, distance, NUM_ANCHORS);
    if (ret == -EBUSY) {
      cout << "Resource busy. Maybe ranging is in progress. Try again later" << endl;
      return ret;
    }
  }

  return 0;
}

