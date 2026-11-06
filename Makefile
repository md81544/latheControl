# Makefile - CMake wrapper
# Usage:
#   make          -> debug build, with mocked GPIO (FAKE), plus the unit tests
#   make release  -> release build against pigpio (Raspberry Pi only)
#   make test     -> debug build, then run the unit tests
#   make latest   -> pull this repo and its submodules, then make release
#   make sub      -> initialise/update the stepperControl submodule
#   make clean    -> remove all build directories and the lc symlink
#
# Debug and Release live in separate trees under BUILD_ROOT, so switching
# between them no longer wipes the other. The lc and compile_commands.json
# symlinks in the source root point at whichever was built last.

APP_NAME := lc

BUILD_ROOT ?= build

BUILD_DIR_DEBUG   := $(BUILD_ROOT)/Debug
BUILD_DIR_RELEASE := $(BUILD_ROOT)/Release

MAKEFLAGS += --no-print-directory

CMAKE       := cmake
CMAKE_FLAGS :=

GENERATOR := Unix Makefiles
MARKER    := Makefile
JOBS      := $(shell nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)

.PHONY: all debug fake release test latest sub clean

# ---------------------------------------------------------------------------
# Default target -> debug
# ---------------------------------------------------------------------------
all: debug

# ---------------------------------------------------------------------------
# Debug build: GPIO, steppers and encoders are mocked (FAKE), so it runs on
# any machine. `fake` is kept as an alias
# ---------------------------------------------------------------------------
debug fake: $(BUILD_DIR_DEBUG)/$(MARKER)
	$(CMAKE) --build $(BUILD_DIR_DEBUG) -- -j$(JOBS)

$(BUILD_DIR_DEBUG)/$(MARKER):
	@$(CMAKE) -E make_directory $(BUILD_DIR_DEBUG)
	$(CMAKE) -S . -B $(BUILD_DIR_DEBUG) -G "$(GENERATOR)" -DCMAKE_BUILD_TYPE=Debug -DFAKE=ON $(CMAKE_FLAGS)

# ---------------------------------------------------------------------------
# Release build: links pigpio, so needs a Raspberry Pi
# ---------------------------------------------------------------------------
release: $(BUILD_DIR_RELEASE)/$(MARKER)
	$(CMAKE) --build $(BUILD_DIR_RELEASE) -- -j$(JOBS)

$(BUILD_DIR_RELEASE)/$(MARKER):
	@$(CMAKE) -E make_directory $(BUILD_DIR_RELEASE)
	$(CMAKE) -S . -B $(BUILD_DIR_RELEASE) -G "$(GENERATOR)" -DCMAKE_BUILD_TYPE=Release -DFAKE=OFF $(CMAKE_FLAGS)

# ---------------------------------------------------------------------------
# Test (debug build + run)
# ---------------------------------------------------------------------------
# Builds the app too, so a passing run means the app is up to date as well
test: debug
	@$(BUILD_DIR_DEBUG)/test/unit_test -d y

# ---------------------------------------------------------------------------
# Update from git, then rebuild for the Pi
# ---------------------------------------------------------------------------
latest:
	git pull --recurse-submodules
	$(MAKE) release

sub:
	git submodule update --init --recursive

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------
clean:
	@$(CMAKE) -E rm -rf $(BUILD_ROOT) $(APP_NAME) compile_commands.json
	@echo "-> build directories and symlinks removed"
