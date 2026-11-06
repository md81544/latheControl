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
find_package(SFML 3 REQUIRED COMPONENTS Graphics Window System)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Options shared by every target (warnings, FAKE)
# ---------------------------------------------------------------------------
add_library(lc_options INTERFACE)
target_compile_options(lc_options INTERFACE -Wall -Wextra -Werror -Wpedantic)
if(FAKE)
	target_compile_definitions(lc_options INTERFACE FAKE)
endif()

# ---------------------------------------------------------------------------
# Code shared between the app and the unit tests
# ---------------------------------------------------------------------------
add_library(shared_with_test OBJECT
	stepperControl/steppermotor.cpp
	rotaryencoder.cpp
	linearscale.cpp
	log.cpp
	model.cpp
)
target_link_libraries(shared_with_test PUBLIC lc_options fmt::fmt Threads::Threads)

# Raspberry Pi 32-bit needs the atomic library
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "armv7l")
	target_link_libraries(shared_with_test PUBLIC atomic)
endif()

# ---------------------------------------------------------------------------
# The app
# ---------------------------------------------------------------------------
add_executable(${APP_NAME}
	stepperControl/gpio.cpp
	view_sfml.cpp
	controller.cpp
	main.cpp
	configreader.cpp
	sfml_dialog.cpp
	axis.cpp
)
target_link_libraries(${APP_NAME} PRIVATE
	shared_with_test
	SFML::Graphics
	SFML::Window
	SFML::System
)
if(NOT FAKE)
	target_link_libraries(${APP_NAME} PRIVATE pigpio)
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
