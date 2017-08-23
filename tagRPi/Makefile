CXX=g++
STD=-std=c++11

# Edit this MLAT path properly
MLAT=/home/pi/mlat/cpp

i2cController: mlat.o i2cController.cpp
	$(CXX) $(STD) -I$(MLAT) -o i2cController mlat.o i2cController.cpp

mlat.o: $(MLAT)/mlat.h $(MLAT)/mlat.cc
	$(CXX) $(STD) -c -o mlat.o $(MLAT)/mlat.cc

clean:
	rm -f mlat.o i2cController

.PHONY: clean

