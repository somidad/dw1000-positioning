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
  data[0] = wiringPiI2CRead(i2cFd);
  if (data[0] == I2C_NODATA) {
    return -EBUSY;
  }
  if (data[0] != I2C_DATARD) {
    return -EINVAL;
  }

  wiringPiI2CWrite(i2cFd, CMD_TYPE_ID);
  for (int i = 0; i < 2 * NUM_ANCHORS; i++) {
    data[i] = wiringPiI2CRead(i2cFd);
  }
  for (int i = 0; i < NUM_ANCHORS; i++) {
    anchorId[i] = ID_NONE;
    #warning "Must check endianess and other things"
    memcpy(anchorId + i, data + 2 * i, 2);
  }

  wiringPiI2CWrite(i2cFd, CMD_TYPE_DIST);
  for (int i = 0; i < 4 * NUM_ANCHORS; i++) {
    data[i] = wiringPiI2CRead(i2cFd);
  }
  for (int i = 0; i < NUM_ANCHORS; i++) {
    #warning "Must check endianess and other things"
    memcpy(distance + i, data + 4 * i, 4);
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
    if (ret == -EINVAL) {
      cout << "Somethings wrong";
      return ret;
    }

    for (int i = 0; i < NUM_ANCHORS; i++) {
      cout << "Anchod ID: " << anchorId[i] << ", Distnace: " << distance[i] << endl;
    }
  }

  return 0;
}

