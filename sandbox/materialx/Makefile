CXX=clang++
CXXFLAGS=-Weverything -Wall

.PHONY: clean

all: pugixml.o
	$(CXX) $(CXXFLAGS) -o materialx_test -I../../src/external -I../../src/external/jsonhpp materialx-main.cc pugixml.o

pugixml.o: ../../src/external/pugixml.cpp
	$(CXX) -o $@ -c $<


clean:
	rm -rf materialx_test pugixml.o
