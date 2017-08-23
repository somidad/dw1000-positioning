CXX=g++
STD=-std=c++11

# Edit this MLAT path properly
MLAT=/home/songsong/mlat/cpp

tagRPi: i2cController.o mlat.o tagRPi.cpp
	$(CXX) $(STD) -I$(MLAT) -o tagRPi i2cController.o mlat.o tagRPi.cpp

i2cController.o: i2cController.h i2cController.cpp
	$(CXX) $(STD) -c -o i2cController.o i2cController.cpp

mlat.o: $(MLAT)/mlat.h $(MLAT)/mlat.cc
	$(CXX) $(STD) -c -o mlat.o $(MLAT)/mlat.cc

clean:
	rm -f i2cController.o mlat.o i2cController

.PHONY: clean

