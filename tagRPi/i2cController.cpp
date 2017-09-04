#include <bitset>
#include <cerrno>
#include <iostream>

// I2C
#include <linux/i2c-dev.h> // I2C_SLAVE
#include <fcntl.h>         // open, O_RDWR
#include <sys/ioctl.h>     // ioctl
#include <unistd.h>        // close

#include "def.h"
#include "i2c.h"
#include "i2cController.h"

using namespace std;

uint8_t cmd_scan = CMD_SCAN;
uint8_t cmd_data_ready = CMD_DATA_READY;
uint8_t cmd_type_id = CMD_TYPE_ID;
uint8_t cmd_type_dist = CMD_TYPE_DIST;

int openI2C(const char* i2cdev, int i2cslaveaddr) {
  int i2cFd = open(i2cdev, O_RDWR);
  if (i2cFd < 0) {
    cout << "Can't open I2C device(" << i2cdev << ")" << endl;
    return EBADF;
  }
  ioctl(i2cFd, I2C_SLAVE, i2cslaveaddr);
  return i2cFd;
}

int triggerScan(int i2cFd) {
  cout << "Triggering scan..." << endl;
  if (write(i2cFd, &cmd_scan, 1) != 1) {
    cout << "Somethings wrong" << endl;
    return -1;
  }
  return 0;
}

bool isReady(int i2cFd) {
  uint8_t data[32];

  cout << "Checking data is ready..." << endl;
  if (write(i2cFd, &cmd_data_ready, 1) != 1) {
    cout << "Write CMD_DATA_READY failed" << endl;
    return false;
  }
  if (read(i2cFd, data, 1) != 1) {
    cout << "Reading failed" << endl;
    return false;
  }
  if (data[0] == I2C_NODATA) {
    cout << "No data available" << endl;
    return false;
  }
  if (data[0] != I2C_DATARD) {
    cout << "Some other value than DATARD"
         << " (received: 0b" << bitset<8>(data[0])
         << ", expected: 0b" << bitset<8>(I2C_DATARD) << ")" << endl;
    return false;
  }
  return true;
}

int getAnchorIds(int i2cFd, uint16_t* anchorId) {
  uint8_t data[32];

  cout << "Getting Anchors' IDs..." << endl;
  if (write(i2cFd, &cmd_type_id, 1) != 1) {
    cout << "  Writing CMD_TYPE_ID failed" << endl;
    return -1;
  }
  if (read(i2cFd, data, 2 * NUM_ANCHORS) != 2 * NUM_ANCHORS) {
    cout << "  Reading Anchors' IDs failed" << endl;
    return -1;
  }
  for (int i = 0; i < NUM_ANCHORS; i++) {
    anchorId[i] = ID_NONE;
    /* Arduino uses little endian */
    anchorId[i] = (data[2 * i + 1] << 8) | data[2 * i + 0];
  }
  return 0;
}

int getDists(int i2cFd, float* distance) {
  uint8_t data[32];

  cout << "Getting distance measurements..." << endl;
  if (write(i2cFd, &cmd_type_dist, 1) != 1) {
    cout << "Writing CMD_TYPE_DIST failed" << endl;
    return -1;
  }
  if (read(i2cFd, data, 4 * NUM_ANCHORS) != 4 * NUM_ANCHORS) {
    cout << "Reading distance measurements failed" << endl;
    return -1;
  }
  for (int i = 0; i < NUM_ANCHORS; i++) {
    /*
     * Arduino uses little endian
     *  Tricky part to assign float value via binary representation
     */
    uint32_t float_binary = (data[4 * i + 3] << 24) | (data[4 * i + 2] << 16)
                          | (data[4 * i + 1] <<  8) | (data[4 * i + 0]      );
    distance[i] = *(float*)&float_binary;
  }
  return 0;
}

int readMeasurement(int i2cFd, uint16_t* anchorId, float* distance) {
  int ret;

  if(!isReady(i2cFd)) {
    return -1;
  }

  ret = getAnchorIds(i2cFd, anchorId);
  if (ret < 0) {
    return ret;
  }

  ret = getDists(i2cFd, distance);
  if (ret < 0) {
    return ret;
  }

  return 0;
}

void getValidMeasurement(uint16_t* anchorId, float* distance,
                        vector<uint16_t>& validAnchors, vector<float>& validDistance) {
  validAnchors.clear();
  validDistance.clear();
  for (int i = 0; i < NUM_ANCHORS; i++) {
    if (anchorId[i] && distance[i]) {
      validAnchors.push_back(anchorId[i]);
      validDistance.push_back(distance[i]);
    }
  }
}
