CC=clang
CXX=clang++
INCFLAGS=-I../../src/external/ryu -I../../src

# only applied to usda-parser.cc
EXTRA_COMPILER_FLAGS=-Weverything -Werror -Wno-padded -Wno-c++98-compat
CXXFLAGS=-std=c++11 -fsanitize=address 
LDFLAGS=-fsanitize=address

.PHONY: clean all

all: usda-parser

usda-parser: usda-parser.o d2s.o f2s.o
	$(CXX) $(LDFLAGS) -o $@ $^
	
usda-parser.o: usda-parser.cc
	$(CXX) $(INCFLAGS) $(CXXFLAGS) $(EXTRA_COMPILER_FLAGS) -c -o $@ $<

d2s.o: ../../src/external/ryu/ryu/d2s.c
	$(CC) $(INCFLAGS) $(CXXFLAGS) -c -o $@ $<

f2s.o: ../../src/external/ryu/ryu/f2s.c
	$(CC) $(INCFLAGS) $(CXXFLAGS) -c -o $@ $<

clean:
	@rm -rf usda-parser usda-parser.o d2s.o f2s.o

