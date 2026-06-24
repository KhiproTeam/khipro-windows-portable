MINGW_CXX ?= x86_64-w64-mingw32-g++
WINDRES ?= x86_64-w64-mingw32-windres

ROOT := $(abspath $(CURDIR))
LIB_INCLUDE := $(ROOT)/resources/khipro/include
LIB_LIB := $(ROOT)/resources/khipro/lib

WIN_CXXFLAGS = -std=c++17 -Os -DNDEBUG -DUNICODE -D_UNICODE -Wall -Wextra \
               -ffunction-sections -fdata-sections -fno-rtti \
               -I$(LIB_INCLUDE)
WIN_LDFLAGS = -Wl,--gc-sections -s -municode -mwindows -static -static-libgcc -static-libstdc++ \
              -L$(LIB_LIB)
WIN_LIBS = -lkhipro -lstdc++ -lwinpthread -lshell32

BUILD_DIR = build
WIN_DIR = $(BUILD_DIR)/win

# Default: offline build against the vendored library in resources/khipro/.
# No network access required. Run `make update-lib` to refresh the vendored
# artifact from KhiproTeam/khipro-library's latest release.
all: khipro.exe

$(WIN_DIR):
	mkdir -p $(WIN_DIR)

$(WIN_DIR)/main.o: src/main.cpp src/resources.h | $(WIN_DIR)
	$(MINGW_CXX) $(WIN_CXXFLAGS) -c $< -o $@

$(WIN_DIR)/khipro.res.o: resources/khipro.rc src/resources.h resources/khipro.manifest | $(WIN_DIR)
	$(WINDRES) $< -O coff -o $@

khipro.exe: $(WIN_DIR)/main.o $(WIN_DIR)/khipro.res.o
	$(MINGW_CXX) $(WIN_CXXFLAGS) $^ -o $@ $(WIN_LDFLAGS) $(WIN_LIBS)

# Manual: pull the latest library release from GitHub and overwrite
# resources/khipro/. Review the diff and commit.
update-lib:
	@bash scripts/update-khipro-lib.sh

clean:
	rm -rf $(BUILD_DIR) khipro.exe

.PHONY: all update-lib clean
