# Makefile for zstd USD compression feature tests
# This is a standalone Makefile that builds and runs the zstd compression tests

CXX = g++
CXXFLAGS = -std=c++14 -O2 -g -Wall -Wextra

# TinyUSDZ source directory
TINYUSDZ_DIR = ../../..
SRC_DIR = $(TINYUSDZ_DIR)/src
EXT_DIR = $(SRC_DIR)/external

# Include paths
INCLUDES = -I$(SRC_DIR) -I$(EXT_DIR)

# Define to enable zstd compression
DEFINES = -DTINYUSDZ_WITH_ZSTD_COMPRESSION -DZSTD_DISABLE_ASM=1

# Zstd source (amalgamated single file)
ZSTD_SRC = $(EXT_DIR)/zstd.c

# TinyUSDZ sources needed for this test
TINYUSDZ_SRCS = \
	$(SRC_DIR)/zstd-compression.cc \
	$(SRC_DIR)/tinyusdz.cc \
	$(SRC_DIR)/usda-writer.cc \
	$(SRC_DIR)/usda-reader.cc \
	$(SRC_DIR)/usdc-reader.cc \
	$(SRC_DIR)/crate-reader.cc \
	$(SRC_DIR)/ascii-parser.cc \
	$(SRC_DIR)/prim-types.cc \
	$(SRC_DIR)/value-types.cc \
	$(SRC_DIR)/pprinter.cc \
	$(SRC_DIR)/value-pprint.cc \
	$(SRC_DIR)/io-util.cc \
	$(SRC_DIR)/str-util.cc \
	$(SRC_DIR)/primvar.cc \
	$(SRC_DIR)/stage.cc \
	$(SRC_DIR)/prim-composition.cc \
	$(SRC_DIR)/composition.cc \
	$(SRC_DIR)/asset-resolution.cc \
	$(SRC_DIR)/path-util.cc \
	$(SRC_DIR)/image-loader.cc \
	$(SRC_DIR)/usdGeom.cc \
	$(SRC_DIR)/usdSkel.cc \
	$(SRC_DIR)/usdShade.cc \
	$(SRC_DIR)/usdLux.cc \
	$(SRC_DIR)/usdRender.cc \
	$(SRC_DIR)/layer.cc \
	$(SRC_DIR)/lz4-compression.cc \
	$(EXT_DIR)/lz4/lz4.c \
	$(EXT_DIR)/fpng.cpp \
	$(EXT_DIR)/string_id/database.cpp \
	$(EXT_DIR)/string_id/error.cpp \
	$(EXT_DIR)/string_id/string_id.cpp \
	$(EXT_DIR)/ryu/s2d.c \
	$(EXT_DIR)/ryu/d2s.c \
	$(EXT_DIR)/tsl/robin_hood.h

# Test source
TEST_SRC = test_zstd_usd.cc

# Output binary
TARGET = test_zstd_usd

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(TEST_SRC) $(TINYUSDZ_SRCS) $(ZSTD_SRC)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) -o $@ $(TEST_SRC) $(TINYUSDZ_SRCS) $(ZSTD_SRC) -lpthread

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) test_output.usda.zst
