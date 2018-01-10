#include <bitset>
#include <cerrno>
#include <cstdint>
#include <iostream>
#include <unistd.h>

#include <eigen3/Eigen/Dense>

#include <mlat.h>

#include "def.h"
#include "i2c.h"
#include "anchorReader.h"
#include "i2cController.h"

using namespace std;
using namespace Eigen;
using namespace mlat;

int main(int argc, char* argv[]) {
  int ret = 0;
  int i2cFd;

  // Read from anchor deployment configuration file
  vector<int> config_anchorId;
  vector<vector<float>> config_anchorPos;
  readAnchors("anchors.csv", config_anchorId, config_anchorPos);

  // Obtained from Arduino
  uint16_t data_anchorId[NUM_ANCHORS];
  float data_distance[NUM_ANCHORS];

  // Filtered from data_{anchorId,distance}
  vector<uint16_t> validAnchors;
  vector<float> validDistance;

  i2cFd = openI2C(I2CDEV, I2CSLAVEADDR);
  if (i2cFd < 0) {
    return 1;
  }
while (true) {
  triggerScan(i2cFd);
  // NOTE, TODO: Tunable value according to (...)_TIMEOUT_MS `arduino.h`
  // wait for scanning and ranging delay (330 ms)
  usleep(330 * 1000);
  cout << "Reading measurement..." << endl;
  ret = readMeasurement(i2cFd, data_anchorId, data_distance);
  if (ret < 0) {
    close(i2cFd);
    return -ret;
  }
  getValidMeasurement(data_anchorId, data_distance, validAnchors, validDistance);
  if (validAnchors.size() < 3) {
    cout << "Valid ranging measurements with anchors fewer than 3. Skip" << endl;
    usleep(50 * 1000);
    continue;
  }

  // preparing and performing mlat
  MatrixXd anchors(validAnchors.size(), 3);
  VectorXd ranges(anchors.rows());
  for (int i = 0; i < anchors.rows(); i++) {
    int idx;
    for (int j = 0; j < config_anchorId.size(); j++) {
      if (validAnchors[i] == config_anchorId[j]) {
        idx = j;
        break;
      }
    }
    for (int j = 0; j < 3; j++) {
      anchors(i, j) = config_anchorPos[idx][j];
    }
    ranges(i) = validDistance[i];
  }
  // TODO: You need to define search space boundary to prevent UNEXPECTED RESULT
  // If not, search space boundary is defined as a cube constrained to
  // minimum and maximum coordinates of x, y, z of anchors
  // If anchors are in the same plane, i.e., all anchors have the same (similar)
  // coordinate of at least one axes, you MUST define search space boundary
  // See https://github.com/gsongsong/mlat
  // So, defining search space boundary is all up to you
  ArrayXXd bounds(2, anchors.rows());
  for (int i = 0; i < anchors.rows(); i++) {
    bounds(0, i) = anchors.col(i).minCoeff(); // minimum boundary of ith axis
    bounds(1, i) = anchors.col(i).maxCoeff(); // maximum boundary of ith axis
  }
  // hard coded height (3 m) of search boundary
  bounds(1, anchors.rows() - 1) = 3;
  MLAT::GdescentResult gdescent_result = MLAT::mlat(anchors, ranges, bounds);

  // print out
  for (int i = 0; i < NUM_ANCHORS; i++) {
    cout << "Anchor ID: " << data_anchorId[i] << " (0b" << bitset<16>(data_anchorId[i]) << ")"
         << ", Distnace: " << data_distance[i] << " (0b" << bitset<32>(*(uint32_t*)&data_distance[i]) << ")" << endl;
  }
  cout << "Estimated position" << endl;
  cout << gdescent_result.estimator << endl;

  // Some random delay for Arduino to idling
  int d = 50;
  usleep(d * 1000);
}

  close(i2cFd);
  return ret;
}
