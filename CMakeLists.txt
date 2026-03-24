cmake_minimum_required(VERSION 3.13)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project (lc)
enable_testing()

add_library(shared_with_test OBJECT
        stepperControl/steppermotor.cpp
        rotaryencoder.cpp
        linearscale.cpp
        log.cpp
        model.cpp
    )
target_compile_options(shared_with_test PRIVATE
    -std=c++23 -Wall -Wextra -Werror -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-O3>
)

add_executable(lc
        stepperControl/gpio.cpp
        view_sfml.cpp
        controller.cpp
        main.cpp
        configreader.cpp
        sfml_dialog.cpp
        axis.cpp
    )

if(DEFINED FAKE)
    add_compile_definitions(FAKE)
    add_subdirectory(test)
else()
    target_link_libraries(lc PRIVATE pigpio)
endif()

target_link_libraries(lc PRIVATE
        shared_with_test
        fmt
        pthread
        sfml-graphics
        sfml-window
        sfml-system
    )

# Raspberry Pi 32-bit needs atomic library:
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "armv7l")
    target_link_libraries(shared_with_test PRIVATE atomic)
endif()

target_compile_options(lc PRIVATE
    -std=c++23 -Wall -Wextra -Werror -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-O3>
)

# Avoid unused variable warning:
set(ignoreMe "${FAKE}")