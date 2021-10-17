cmake_minimum_required(VERSION 3.5)

project (lc)

add_library(shared_with_test OBJECT
	stepperControl/steppermotor.cpp
	rotaryencoder.cpp
	log.cpp
	model.cpp
    )

add_executable(lc
	stepperControl/gpio.cpp
	view_sfml.cpp
	controller.cpp
	main.cpp
	configreader.cpp
    )

add_compile_definitions(FAKE)

target_link_libraries(lc PRIVATE
	shared_with_test
	fmt
	pthread
	sfml-graphics
	sfml-window
	sfml-system
    )

target_compile_options(lc PRIVATE -std=c++17 -Wall -Wextra -Werror -Wpedantic)

add_subdirectory(test)
