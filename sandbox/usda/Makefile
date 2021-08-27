CC=clang
CXX=clang++
INCFLAGS=-I. -I../../src/external/ryu -I../../src

# only applied to usda-parser.cc
EXTRA_COMPILER_FLAGS=-Weverything -Werror -Wno-padded -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-unused-function -Wno-unused-member-function
CXXFLAGS=-std=c++14 -fsanitize=address -g -O0 -DUSDA_MAIN -DTINYUSDZ_USE_USDOBJ
CFLAGS=-fsanitize=address -g -O0
LDFLAGS=-fsanitize=address

# On some macOS
#EXTRA_COMPILER_FLAGS+=-Wno-poison-system-directories

# gcc
#EXTRA_COMPILER_FLAGS=-Wall
#CXXFLAGS=-std=c++11 -g -O0
#CFLAGS=-g -O0
#LDFLAGS=

.PHONY: clean all t

all: usda-parser t

# unit tester
u: usda-parser
	python unit-runner.py	

# single test
t: usda-parser
	./usda-parser ../../models/usdObj-001.usda
	#./usda-parser tests/dict.usda
	#./usda-parser tests/simple-blender-exported.usda
	#./usda-parser tests/test.usda

usda-parser: usda-parser.o d2s.o f2s.o s2f.o s2d.o simple-serialize.o prim-types.o usdObj.o io-util.o
	$(CXX) $(LDFLAGS) -o $@ $^
	
usda-parser.o: ../../src/usda-parser.cc ../../src/simple-serialize.hh ../../src/prim-types.hh 
	$(CXX) $(INCFLAGS) $(CXXFLAGS) $(EXTRA_COMPILER_FLAGS) -c -o $@ $<

prim-types.o: ../../src/prim-types.cc ../../src/prim-types.hh
	$(CXX) $(INCFLAGS) $(CXXFLAGS) $(EXTRA_COMPILER_FLAGS) -c -o $@ $<

simple-serialize.o: ../../src/simple-serialize.cc ../../src/simple-serialize.hh
	$(CXX) $(INCFLAGS) $(CXXFLAGS) $(EXTRA_COMPILER_FLAGS) -c -o $@ $<

usdObj.o: ../../src/usdObj.cc ../../src/usdObj.hh
	$(CXX) $(INCFLAGS) $(CXXFLAGS) $(EXTRA_COMPILER_FLAGS) -c -o $@ $<

io-util.o: ../../src/io-util.cc ../../src/io-util.hh
	$(CXX) $(INCFLAGS) $(CXXFLAGS) $(EXTRA_COMPILER_FLAGS) -c -o $@ $<

d2s.o: ../../src/external/ryu/ryu/d2s.c
	$(CC) $(INCFLAGS) $(CFLAGS) -c -o $@ $<

f2s.o: ../../src/external/ryu/ryu/f2s.c
	$(CC) $(INCFLAGS) $(CFLAGS) -c -o $@ $<

s2f.o: ../../src/external/ryu/ryu/s2f.c
	$(CC) $(INCFLAGS) $(CFLAGS) -c -o $@ $<

s2d.o: ../../src/external/ryu/ryu/s2d.c
	$(CC) $(INCFLAGS) $(CFLAGS) -c -o $@ $<

#file.o: lexy/file.cpp
#	$(CXX) $(INCFLAGS) $(CXXFLAGS) -c -o $@ $<

clean:
	@rm -rf usda-parser usda-parser.o d2s.o f2s.o s2d.o s2f.o simple-serialize.o prim-types.o usdObj.o io-util.o

