cmake_minimum_required(VERSION 3.20)

set(APP_NAME lc)
project(${APP_NAME} CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release)
endif()

# FAKE mocks out pigpio, the steppers and the rotary encoders, so the app runs
# on a PC; it also builds the unit tests. The Makefile turns it on for Debug
option(FAKE "Mock the GPIO hardware and build the unit tests" OFF)

find_package(fmt REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# pigpio, for the real GPIO (gpio.cpp). Only on a Raspberry Pi: FAKE builds
# use MockGpio instead, so don't need it
# ---------------------------------------------------------------------------
if(NOT FAKE)
	find_path(PIGPIO_INCLUDE_DIR pigpio.h)
	find_library(PIGPIO_LIBRARY pigpio)
	if(NOT PIGPIO_INCLUDE_DIR OR NOT PIGPIO_LIBRARY)
		message(FATAL_ERROR
			"pigpio not found. It is only available on a Raspberry Pi; on other "
			"machines build with FAKE=ON (make, rather than make release)")
	endif()
endif()

# ---------------------------------------------------------------------------
# Options shared by every target (warnings, FAKE)
# ---------------------------------------------------------------------------
add_library(lc_options INTERFACE)
target_compile_options(lc_options INTERFACE -Wall -Wextra -Werror -Wpedantic
	# Designated initializers that leave out members with defaults, e.g.
	# MultiPassSetup { .startMemory = 0, .cutDistance = 1.0 }, are intended.
	# GCC (on the Pi) warns about them under -Wextra; clang doesn't
	-Wno-missing-field-initializers)
if(FAKE)
	target_compile_definitions(lc_options INTERFACE FAKE)
endif()

# ---------------------------------------------------------------------------
# Code shared between the app and the unit tests
# ---------------------------------------------------------------------------
add_library(shared_with_test OBJECT
	rotaryencoder.cpp
	linearscale.cpp
	joystick.cpp
	commands.cpp
	gearcutting.cpp
	menu.cpp
	log.cpp
	model.cpp
	motionsequence.cpp
	multipass.cpp
	radius.cpp
	steppermotor.cpp
	threadpitches.cpp
	cpuaffinity.cpp
)
target_link_libraries(shared_with_test PUBLIC
	lc_options
	fmt::fmt
	Threads::Threads
)
# The Raspberry Pi's 32-bit OS needs the atomic library (for
# std::atomic<double> etc, used by the stepper motor code)
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "armv7l")
	target_link_libraries(shared_with_test PUBLIC atomic)
endif()

# ---------------------------------------------------------------------------
# The app
# ---------------------------------------------------------------------------
add_executable(${APP_NAME}
	view_qt.cpp
	qt_dialog.cpp
	controller.cpp
	main.cpp
	configreader.cpp
)
target_link_libraries(${APP_NAME} PRIVATE
	shared_with_test
	Qt6::Widgets
)
# The real GPIO (main.cpp uses MockGpio in FAKE builds)
if(NOT FAKE)
	target_sources(${APP_NAME} PRIVATE gpio.cpp)
	target_include_directories(${APP_NAME} PRIVATE ${PIGPIO_INCLUDE_DIR})
	target_link_libraries(${APP_NAME} PRIVATE ${PIGPIO_LIBRARY})
endif()

# ---------------------------------------------------------------------------
# Symlinks in the source root to the last-built executable (it loads lc.cfg
# and lc_font.ttf from the current directory, so is run from here) and to
# compile_commands.json, for clangd and VS Code
# ---------------------------------------------------------------------------
add_custom_command(
	TARGET ${APP_NAME}
	POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE:${APP_NAME}>
		${CMAKE_SOURCE_DIR}/${APP_NAME}
	COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_BINARY_DIR}/compile_commands.json
		${CMAKE_SOURCE_DIR}/compile_commands.json
	COMMENT "Updating symlinks: ${APP_NAME}, compile_commands.json"
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(FAKE)
	enable_testing()
	add_subdirectory(test)
endif()
