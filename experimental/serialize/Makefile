CXX=clang++
CXXFLAGS=-std=c++11 -I../../src -fsanitize=address -Weverything -Wall -Wno-padded -Wno-c++98-compat -Wno-c++98-compat-pedantic
LINKFLAGS=-fsanitize=address

all: serializer

serializer: tinyusdz.o serializer.o integerCoding.o lz4-compression.o lz4.o
	$(CXX) $(LINKFLAGS) -o serializer $^

serializer.o: ../../src/tinyusdz.cc ../../src/tinyusdz.hh serializer.cc
	$(CXX) $(CXXFLAGS) -c -o serializer.o serializer.cc	

tinyusdz.o: ../../src/tinyusdz.cc ../../src/tinyusdz.hh
	$(CXX) $(CXXFLAGS) -c -o tinyusdz.o ../../src/tinyusdz.cc

integerCoding.o: ../../src/integerCoding.cpp 
	$(CXX) $(CXXFLAGS) -c -o integerCoding.o ../../src/integerCoding.cpp

lz4-compression.o: ../../src/lz4-compression.cc
	$(CXX) $(CXXFLAGS) -c -o lz4-compression.o ../../src/lz4-compression.cc

lz4.o: ../../src/pxrLZ4/lz4.cpp 
	$(CXX) $(CXXFLAGS) -c -o lz4.o ../../src/pxrLZ4/lz4.cpp

.PHONY: clean

clean:
	rm -rf serializer lz4.o serializer.o integerCoding.o tinyusdz.o lz4-compression.o
