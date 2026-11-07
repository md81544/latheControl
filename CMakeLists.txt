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
# stepperControl library, fetched at configure time. To build against a local
# checkout instead (e.g. when working on both), use
#   make STEPPERCONTROL_DIR=../stepperControl
# which sets FETCHCONTENT_SOURCE_DIR_STEPPERCONTROL
# ---------------------------------------------------------------------------
include(FetchContent)
set(STEPPERCONTROL_GIT_REPOSITORY "https://github.com/md81544/stepperControl.git"
	CACHE STRING "Git repository to fetch stepperControl from")
FetchContent_Declare(stepperControl
	GIT_REPOSITORY ${STEPPERCONTROL_GIT_REPOSITORY}
	GIT_TAG        v1.3.0
	GIT_SHALLOW    TRUE
)
# FAKE builds use MockGpio, so don't need pigpio
if(FAKE)
	set(STEPPERCONTROL_USE_PIGPIO OFF)
else()
	set(STEPPERCONTROL_USE_PIGPIO ON)
endif()
FetchContent_MakeAvailable(stepperControl)

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
	rotaryencoder.cpp
	linearscale.cpp
	joystick.cpp
	commands.cpp
	menu.cpp
	log.cpp
	model.cpp
	motionsequence.cpp
	multipass.cpp
	cpuaffinity.cpp
)
target_link_libraries(shared_with_test PUBLIC
	lc_options
	fmt::fmt
	Threads::Threads
	stepperControl::stepperControl
)

# ---------------------------------------------------------------------------
# The app
# ---------------------------------------------------------------------------
add_executable(${APP_NAME}
	view_qt.cpp
	qt_dialog.cpp
	controller.cpp
	main.cpp
	configreader.cpp
	axis.cpp
)
target_link_libraries(${APP_NAME} PRIVATE
	shared_with_test
	Qt6::Widgets
)

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
