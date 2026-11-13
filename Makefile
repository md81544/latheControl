# Makefile - CMake wrapper
# Usage:
#   make          -> debug build, with mocked GPIO (FAKE), plus the unit tests
#   make release  -> release build against pigpio (Raspberry Pi only)
#   make test     -> debug build, then run the unit tests
#   make latest   -> pull this repo, then make release
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

.PHONY: all debug fake release test latest clean

# $(call configure,<build dir>,<build type>,<FAKE ON/OFF>)
# Runs CMake's configure step if the build tree hasn't been configured yet.
# (After that, CMake reconfigures by itself when CMakeLists.txt changes)
define configure
	@if [ ! -f $(1)/$(MARKER) ]; then \
		echo "$(CMAKE) -S . -B $(1) -DCMAKE_BUILD_TYPE=$(2) -DFAKE=$(3) $(CMAKE_FLAGS)" && \
		$(CMAKE) -S . -B $(1) -G "$(GENERATOR)" -DCMAKE_BUILD_TYPE=$(2) -DFAKE=$(3) \
			$(CMAKE_FLAGS); \
	fi
endef

# ---------------------------------------------------------------------------
# Default target -> debug
# ---------------------------------------------------------------------------
all: debug

# ---------------------------------------------------------------------------
# Debug build: GPIO, steppers and encoders are mocked (FAKE), so it runs on
# any machine. `fake` is kept as an alias
# ---------------------------------------------------------------------------
debug fake:
	$(call configure,$(BUILD_DIR_DEBUG),Debug,ON)
	$(CMAKE) --build $(BUILD_DIR_DEBUG) -- -j$(JOBS)

# ---------------------------------------------------------------------------
# Release build: links pigpio, so needs a Raspberry Pi
# ---------------------------------------------------------------------------
release:
	$(call configure,$(BUILD_DIR_RELEASE),Release,OFF)
	$(CMAKE) --build $(BUILD_DIR_RELEASE) -- -j$(JOBS)

# ---------------------------------------------------------------------------
# Test (debug build + run)
# ---------------------------------------------------------------------------
# Builds the app too, so a passing run means the app is up to date as well
test: debug
	@$(BUILD_DIR_DEBUG)/test/unit_test -d y
	@$(BUILD_DIR_DEBUG)/test/ui_test -d y

# ---------------------------------------------------------------------------
# Update from git, then rebuild for the Pi
# ---------------------------------------------------------------------------
latest:
	git pull
	$(MAKE) release

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------
clean:
	@$(CMAKE) -E rm -rf $(BUILD_ROOT) $(APP_NAME) compile_commands.json
	@echo "-> build directories and symlinks removed"
