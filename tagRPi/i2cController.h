#ifndef I2C_CONTROLLER_H
#define I2C_CONTROLLER_H

#include <cstdint>
#include <vector>

#define I2CDEV "/dev/i2c-1"

using namespace std;

int openI2C(const char* i2cdev, int i2cslaveaddr);
int triggerScan(int i2cFd);
bool isReady(int i2cFd);
int getAnchorIds(int i2cFd, uint16_t* anchorId);
int getDists(int i2cFd, float* distance);
int readMeasurement(int i2cFd, uint16_t* anchorId, float* distance);
void getValidMeasurement(uint16_t* anchorId, float* distance,
                         vector<uint16_t>& validAnchors,
                         vector<float>& validDistance);

#endif /* I2C_CONTROLLER_H */

