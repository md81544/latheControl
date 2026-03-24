MAKEFLAGS += --silent

BUILD_DIR := build
VERBOSE   ?=

# Derive cmake/make flags
CMAKE_BUILD_TYPE = $(if $(findstring release,$(MAKECMDGOALS)),Release,Debug)
CMAKE_FAKE       = $(if $(filter all debug fake,$(or $(MAKECMDGOALS),all)),-DFAKE=1,)

.PHONY: all debug fake release clean test sub latest

# ── Default target ────────────────────────────────────────────────────────────
all: debug

# ── Build targets ─────────────────────────────────────────────────────────────
debug fake: _guard_debug _cmake
	cmake --build $(BUILD_DIR)
	$(call _relink)

release: _guard_release _cmake
	cmake --build $(BUILD_DIR)
	$(call _relink)

test: debug
	$(BUILD_DIR)/test/unit_test -d y

latest:
	git pull --recurse-submodules
	$(MAKE) release

sub:
	git submodule update --init --recursive

clean:
	rm -rf $(BUILD_DIR)/*

# ── Internal helpers ──────────────────────────────────────────────────────────

_guard_debug:
	@if [ -f $(BUILD_DIR)/RELEASE ]; then rm -rf $(BUILD_DIR)/*; fi
	@mkdir -p $(BUILD_DIR) && touch $(BUILD_DIR)/DEBUG

_guard_release:
	@if [ -f $(BUILD_DIR)/DEBUG ]; then rm -rf $(BUILD_DIR)/*; fi
	@mkdir -p $(BUILD_DIR) && touch $(BUILD_DIR)/RELEASE

_cmake: $(BUILD_DIR)/CMakeCache.txt
$(BUILD_DIR)/CMakeCache.txt:
	cd $(BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) $(CMAKE_FAKE) ..

define _relink
	rm -f lc && ln -s $(BUILD_DIR)/lc lc
endef