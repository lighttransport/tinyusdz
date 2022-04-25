CXX=clang++
CXXFLAGS=-Weverything -Wall

.PHONY: clean

all: tinyxml2.o
	$(CXX) $(CXXFLAGS) -o materialx_test -I../../src/external -I../../src/external/jsonhpp materialx-main.cc tinyxml2.o

tinyxml2.o: ../../src/external/tinyxml2/tinyxml2.cpp
	$(CXX) -o $@ -c $<


clean:
	rm -rf materialx_test tinyxml2.o
