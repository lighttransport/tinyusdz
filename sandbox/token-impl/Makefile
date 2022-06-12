CXXFLAGS=-fno-rtti -fno-exceptions

.PHONY: clean

all: error.o string_id.o database.o 
	clang++ $(CXXFLAGS) -I../../src/external -I../../src main.cc $^

error.o: ../../src/external/string_id/error.cpp
	clang++ $(CXXFLAGS) -c -o $@ $<

string_id.o: ../../src/external/string_id/string_id.cpp
	clang++ $(CXXFLAGS) -c -o $@ $<

database.o: ../../src/external/string_id/database.cpp
	clang++ $(CXXFLAGS) -c -o $@ $<


clean:
	rm error.o string_id.o database.o


