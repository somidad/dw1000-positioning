#include <bitset>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>

#include <eigen3/Eigen/Dense>

#include <mlat.h>

#include "def.h"
#include "i2c.h"
#include "i2cController.h"

using namespace std;
using namespace Eigen;
using namespace mlat;

void printUsage() {
  cout << "Usage" << endl;
  cout << "  tagRPi scan: commands Arduino to scan" << endl;
  cout << "  tagRPi read: requests Arduino for ranging data" << endl;
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

int main(int argc, char* argv[]) {
  int ret = 0;
  if (argc < 2) {
     printUsage();
     return EINVAL;
  }

  int i2cFd = openI2C(I2CDEV, I2CSLAVEADDR);

  uint16_t anchorId[NUM_ANCHORS] = {0, };
  float distance[NUM_ANCHORS] = {0, };

  if (!strcmp(argv[1], "scan")) {
    triggerScan(i2cFd);
  }

  if (!strcmp(argv[1], "calc")) {
    cout << "Reading measurement..." << endl;
    ret = readMeasurement(i2cFd, anchorId, distance);
    if (ret == -EBUSY) {
      cout << "Resource busy. Maybe ranging is in progress. Try again later" << endl;
      goto out;
    }
    if (ret == -EINVAL) {
      cout << "Somethings wrong" << endl;
      goto out;
    }
    if (ret < 0) {
      cout << "Somethings wrong" << endl;
      goto out;
    }

    ifstream anchorFile("anchors.csv");
    vector<int> anchorIdsInFile;
    vector<vector<float>> anchorPosition;
    string value;
    while (anchorFile.good()) {
      getline(anchorFile, value, ',');
      if (value == "\n") {
        continue;
      }
      anchorIdsInFile.push_back(stoi(value));

      vector<float> pos;
      for (int i = 0; i < 2; i++) {
        getline(anchorFile, value, ',');
        pos.push_back(stof(value));
      }
      getline(anchorFile, value);
      pos.push_back(stof(value));
      anchorPosition.push_back(pos);
    }
    for (int i = 0; i < NUM_ANCHORS; i++) {
      cout << "Anchor ID: " << anchorId[i] << " (0b" << bitset<16>(anchorId[i]) << ")"
           << ", Distnace: " << distance[i] << " (0b" << bitset<32>(*(uint32_t*)&distance[i]) << ")" << endl;
    }
    vector<uint16_t> validAnchors;
    vector<float> validDistance;
    getValidMeasurement(anchorId, distance, validAnchors, validDistance);
    if (validAnchors.size() < 3) {
      cout << "Ranging measurements with anchors fewer than 3. Exit" << endl;
      goto out;
    }
    MatrixXd anchors(validAnchors.size(), 3);
    VectorXd ranges(anchors.rows());
    for (int i = 0; i < anchors.rows(); i++) {
      int idx;
      for (int j = 0; j < anchorIdsInFile.size(); j++) {
        if (validAnchors[i] == anchorIdsInFile[j]) {
          idx = j;
          break;
        }
      }
      anchors(i, 0) = anchorPosition[idx][0];
      anchors(i, 1) = anchorPosition[idx][1];
      anchors(i, 2) = anchorPosition[idx][2];
      ranges(i) = validDistance[i];
    }
    MLAT::GdescentResult gdescent_result = MLAT::mlat(anchors, ranges);
    cout << "Estimated position" << endl;
    cout << gdescent_result.estimator << endl;
  }

out:
  close(i2cFd);

  return ret;
}

