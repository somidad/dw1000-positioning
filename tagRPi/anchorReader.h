#ifndef ANCHOR_READER_H
#define ANCHOR_READER_H

#include <vector>

using namespace std;

int readAnchors(char* anchorFileName,
                vector<int>& config_anchorId,
                vector<vector<float>>& config_anchorPos);

#endif /* ANCHOR_READER_H */