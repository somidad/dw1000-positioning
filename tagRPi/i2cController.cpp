#include <bitset>
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

int isReady(int i2cFd) {
  uint8_t data[32];

  cout << "Checking data is ready..." << endl;
  if (write(i2cFd, &cmd_data_ready, 1) != 1) {
    cout << "Write CMD_DATA_READY failed" << endl;
    return -1;
  }
  if (read(i2cFd, data, 1) != 1) {
    cout << "Reading failed" << endl;
    return -1;
  }
  if (data[0] == I2C_NODATA) {
    cout << "No data available" << endl;
    return -EBUSY;
  }
  if (data[0] != I2C_DATARD) {
    cout << "Some other value than DATARD"
         << " (received: 0b" << bitset<8>(data[0])
         << ", expected: 0b" << bitset<8>(I2C_DATARD) << ")" << endl;
    return -EINVAL;
  }
}

int getAnchorIds(int i2cFd, uint16_t* anchorId, int num_anchors) {
  uint8_t data[32];

  cout << "Getting Anchors' IDs..." << endl;
  if (write(i2cFd, &cmd_type_id, 1) != 1) {
    cout << "Writing CMD_TYPE_ID failed" << endl;
    return -1;
  }
  if (read(i2cFd, data, 2 * num_anchors) != 2 * num_anchors) {
    cout << "Reading Anchors' IDs failed" << endl;
    return -1;
  }
  for (int i = 0; i < num_anchors; i++) {
    anchorId[i] = ID_NONE;
    /*& Arduino uses little endian */
    anchorId[i] = (data[2 * i + 1] << 8) | data[2 * i + 0];
  }
}

int getDists(int i2cFd, float* distance, int num_anchors) {
  uint8_t data[32];

  cout << "Getting distance measurements..." << endl;
  if (write(i2cFd, &cmd_type_dist, 1) != 1) {
    cout << "Writing CMD_TYPE_DIST failed" << endl;
    return -1;
  }
  if (read(i2cFd, data, 4 * num_anchors) != 4 * num_anchors) {
    cout << "Reading distance measurements failed" << endl;
    return -1;
  }
  for (int i = 0; i < num_anchors; i++) {
    /* Arduino uses little endian */
    uint32_t float_binary = (data[4 * i + 3] << 24) | (data[4 * i + 2] << 16)
                          | (data[4 * i + 1] <<  8) | (data[4 * i + 0]      );

    distance[i] = *(float*)&float_binary;
  }
}

int readMeasurement(int i2cFd, uint16_t* anchorId, float* distance, int num_anchors) {
  int ret;
  uint8_t data[32];

  ret = isReady(i2cFd);
  if (ret < 0) {
    return ret;
  }

  ret = getAnchorIds(i2cFd, anchorId, num_anchors);
  if (ret < 0) {
    return ret;
  }

  ret = getDists(i2cFd, distance, num_anchors);
  if (ret < 0) {
    return ret;
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

  if (!strcmp(argv[1], "scan")) {
    cout << "Triggering scan..." << endl;
    if (write(i2cFd, &cmd_scan, 1) != 1) {
      cout << "Somethings wrong" << endl;
    }
  }
  if (!strcmp(argv[1], "read")) {
    cout << "Reading measurement..." << endl;
    int ret = readMeasurement(i2cFd, anchorId, distance, NUM_ANCHORS);
    if (ret == -EBUSY) {
      cout << "Resource busy. Maybe ranging is in progress. Try again later" << endl;
      return ret;
    }
    if (ret == -EINVAL) {
      cout << "Somethings wrong" << endl;
      return ret;
    }
    if (ret < 0) {
      cout << "Somethings wrong" << endl;
      return ret;
    }

    for (int i = 0; i < NUM_ANCHORS; i++) {
      cout << "Anchor ID: " << anchorId[i] << " (0b" << bitset<16>(anchorId[i]) << ")"
           << ", Distnace: " << distance[i] << " (0b" << bitset<32>(*(uint32_t*)&distance[i]) << ")" << endl;
    }
  }
  close(i2cFd);

  return 0;
}

