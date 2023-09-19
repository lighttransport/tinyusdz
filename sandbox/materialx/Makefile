CXX=clang++
CXXFLAGS=-Weverything -Wall -Wno-c++98-compat

.PHONY: clean

all: pugixml.o
	$(CXX) $(CXXFLAGS) -o materialx_test -I../../src/external -I../../src/external/jsonhpp materialx-main.cc pugixml.o

pugixml.o: ../../src/external/pugixml.cpp
	$(CXX) -o $@ -c $<


t:
	./materialx_test ../../data/materialx/StandardSurface/standard_surface_wood_tiled.mtlx

clean:
	rm -rf materialx_test pugixml.o
