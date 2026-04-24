CXX ?= g++
MINGW_CXX ?= x86_64-w64-mingw32-g++
WINDRES ?= x86_64-w64-mingw32-windres

SRC = src/main.cpp src/mim_engine.cpp
ENGINE_SRC = src/mim_engine.cpp

WIN_CXXFLAGS = -std=c++17 -Os -DNDEBUG -DUNICODE -D_UNICODE -Wall -Wextra -ffunction-sections -fdata-sections -fno-rtti
WIN_LDFLAGS = -Wl,--gc-sections -s -municode -mwindows -static -static-libgcc -static-libstdc++
WIN_LIBS = -lshell32

NATIVE_CXXFLAGS = -std=c++17 -Os -Wall -Wextra

BUILD_DIR = build
WIN_DIR = $(BUILD_DIR)/win
NATIVE_DIR = $(BUILD_DIR)/native

all: clean khipro.exe

$(WIN_DIR):
	mkdir -p $(WIN_DIR)

$(NATIVE_DIR):
	mkdir -p $(NATIVE_DIR)

$(WIN_DIR)/main.o: src/main.cpp src/mim_engine.h src/resources.h | $(WIN_DIR)
	$(MINGW_CXX) $(WIN_CXXFLAGS) -c $< -o $@

$(WIN_DIR)/mim_engine.o: src/mim_engine.cpp src/mim_engine.h | $(WIN_DIR)
	$(MINGW_CXX) $(WIN_CXXFLAGS) -c $< -o $@

$(WIN_DIR)/khipro.res.o: resources/khipro.rc src/resources.h data/bn-khipro.mim resources/khipro.manifest | $(WIN_DIR)
	$(WINDRES) $< -O coff -o $@

khipro.exe: $(WIN_DIR)/main.o $(WIN_DIR)/mim_engine.o $(WIN_DIR)/khipro.res.o
	$(MINGW_CXX) $(WIN_CXXFLAGS) $^ -o $@ $(WIN_LDFLAGS) $(WIN_LIBS)

$(NATIVE_DIR)/engine_tests: tests/engine_tests.cpp $(ENGINE_SRC) src/mim_engine.h | $(NATIVE_DIR)
	$(CXX) $(NATIVE_CXXFLAGS) tests/engine_tests.cpp $(ENGINE_SRC) -o $@

test: $(NATIVE_DIR)/engine_tests
	cd $(CURDIR) && ./$(NATIVE_DIR)/engine_tests

clean:
	rm -rf $(BUILD_DIR) khipro.exe

.PHONY: all test clean
