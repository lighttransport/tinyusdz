CXX ?= g++
CXXFLAGS = -std=c++14 -Wall -Wextra -O2 -pthread
CXXFLAGS_DEBUG = -std=c++14 -Wall -Wextra -g -O0 -pthread -fsanitize=thread
CXXFLAGS_NO_EXCEPT = -std=c++14 -Wall -Wextra -O2 -pthread -fno-exceptions -fno-rtti

TARGET = task_queue_example
TARGET_DEBUG = task_queue_example_debug
TARGET_NO_EXCEPT = task_queue_example_no_except

all: $(TARGET)

$(TARGET): example.cc task-queue.hh
	$(CXX) $(CXXFLAGS) -o $@ example.cc

debug: $(TARGET_DEBUG)

$(TARGET_DEBUG): example.cc task-queue.hh
	$(CXX) $(CXXFLAGS_DEBUG) -o $@ example.cc

no-except: $(TARGET_NO_EXCEPT)

$(TARGET_NO_EXCEPT): example.cc task-queue.hh
	$(CXX) $(CXXFLAGS_NO_EXCEPT) -o $@ example.cc

run: $(TARGET)
	./$(TARGET)

run-no-except: $(TARGET_NO_EXCEPT)
	./$(TARGET_NO_EXCEPT)

clean:
	rm -f $(TARGET) $(TARGET_DEBUG) $(TARGET_NO_EXCEPT) test_no_exceptions

.PHONY: all debug no-except run run-no-except clean
