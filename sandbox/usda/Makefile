CC=clang
CXX=clang++
INCFLAGS=-I. -I../../src/external/ryu -I../../src

# only applied to usda-parser.cc
EXTRA_COMPILER_FLAGS=-Weverything -Werror -Wno-padded -Wno-c++98-compat
CXXFLAGS=-std=c++11 -fsanitize=address -g -O0
CFLAGS=-fsanitize=address -g -O0
LDFLAGS=-fsanitize=address

# On some macOS
EXTRA_COMPILER_FLAGS+=-Wno-poison-system-directories

# gcc
#EXTRA_COMPILER_FLAGS=-Wall
#CXXFLAGS=-std=c++11 -g -O0
#CFLAGS=-g -O0
#LDFLAGS=

.PHONY: clean all t

all: usda-parser t

t: usda-parser
	./usda-parser tests/simple.usda
	#./usda-parser tests/test.usda

usda-parser: usda-parser.o d2s.o f2s.o s2f.o s2d.o simple-serialize.o
	$(CXX) $(LDFLAGS) -o $@ $^
	
usda-parser.o: usda-parser.cc ../../src/simple-serialize.hh
	$(CXX) $(INCFLAGS) $(CXXFLAGS) $(EXTRA_COMPILER_FLAGS) -c -o $@ $<

simple-serialize.o: ../../src/simple-serialize.cc ../../src/simple-serialize.hh
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
	@rm -rf usda-parser usda-parser.o d2s.o f2s.o s2d.o s2f.o simple-serialize.o

