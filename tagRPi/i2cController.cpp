#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>

// I2C
#include <linux/i2c-dev.h> // I2C_SLAVE
#include <fcntl.h>         // open, O_RDWR
#include <sys/ioctl.h>     // ioctl
#include <unistd.h>        // close
#define I2CDEV "/dev/i2c-1"
#define I2CSLAVEADDR 0x04

#include "def.h"
#include "i2c.h"

#define NUM_ANCHORS 5

using namespace std;

uint8_t cmd_scan = CMD_SCAN;
uint8_t cmd_data_ready = CMD_DATA_READY;
uint8_t cmd_type_id = CMD_TYPE_ID;
uint8_t cmd_type_dist = CMD_TYPE_DIST;

void printUsage() {
  cout << "Usage" << endl;
  cout << "  i2cController scan: commands Arduino to scan" << endl;
  cout << "  i2cController read: requests Arduino for ranging data" << endl;
}

int readMeasurement(int i2cFd, uint16_t* anchorId, float* distance, int num_anchors) {
  uint8_t data[32];

  if (write(i2cFd, &cmd_data_ready, 1) != 1) {
    return -1;
  }
  if (read(i2cFd, data, 1) != 1) {
    return -1;
  }
  if (data[0] == I2C_NODATA) {
    return -EBUSY;
  }
  if (data[0] != I2C_DATARD) {
    return -EINVAL;
  }

  if (write(i2cFd, &cmd_type_id, 1) != 1) {
    return -1;
  }
  if (read(i2cFd, data, 2 * NUM_ANCHORS) != 2 * NUM_ANCHORS) {
    return -1;
  }
  for (int i = 0; i < NUM_ANCHORS; i++) {
    anchorId[i] = ID_NONE;
    /*& Arduino uses little endian */
    anchorId[i] = (data[1] << 8) | data[0];
  }

  if (write(i2cFd, &cmd_type_dist, 1) != 1) {
    return -1;
  }
  if (read(i2cFd, data, 4 * NUM_ANCHORS) != 4 * NUM_ANCHORS) {
    return -1;
  }
  for (int i = 0; i < NUM_ANCHORS; i++) {
    /* Arduino uses little endian */
    distance[i] = (data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0];
  }

  return 0;
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
     printUsage();
     return EINVAL;
  }

  int i2cFd = open(I2CDEV, O_RDWR);
  if (i2cFd < 0) {
    cout << "Can't open I2C device (" << I2CDEV << ")" << endl;
    return EBADF;
  }
  ioctl(i2cFd, I2C_SLAVE, I2CSLAVEADDR);

  uint16_t anchorId[NUM_ANCHORS] = {0, };
  float distance[NUM_ANCHORS] = {0, };

  if (strcmp(argv[1], "scan")) {
    write(i2cFd, &cmd_scan, 1);
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
    if (ret < 0) {
      cout << "Somethings wrong";
      return ret;
    }

    for (int i = 0; i < NUM_ANCHORS; i++) {
      cout << "Anchod ID: " << anchorId[i] << ", Distnace: " << distance[i] << endl;
    }
  }
  close(i2cFd);

  return 0;
}

