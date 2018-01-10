#include <fstream>

#include "anchorReader.h"

using namespace std;

int readAnchors(const char* anchorFileName,
                vector<int>& config_anchorId,
                vector<vector<float>>& config_anchorPos) {
  ifstream anchorFile(anchorFileName);
  string value;

  while (anchorFile.good()) {
    getline(anchorFile, value, ',');
    if (value == "\n" || value.empty()) {
      continue;
    }
    config_anchorId.push_back(stoi(value));

    vector<float> pos;
    for (int i = 0; i < 2; i++) {
      getline(anchorFile, value, ',');
      pos.push_back(stof(value));
    }
    getline(anchorFile, value);
    pos.push_back(stof(value));
    config_anchorPos.push_back(pos);
  }
  return 0;
}
