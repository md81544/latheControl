cmake_minimum_required(VERSION 3.13)

project (lc)
enable_testing()

add_library(shared_with_test OBJECT
        stepperControl/steppermotor.cpp
        rotaryencoder.cpp
        linearscale.cpp
        log.cpp
        model.cpp
    )
target_compile_options(shared_with_test PRIVATE -std=c++23 -Wall -Wextra -Werror -Wpedantic)

add_executable(lc
        stepperControl/gpio.cpp
        view_sfml.cpp
        controller.cpp
        main.cpp
        configreader.cpp
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

target_compile_options(lc PRIVATE -std=c++23 -Wall -Wextra -Werror -Wpedantic)


# Avoid unused variable warning:
set(ignnoreMe "${FAKE}")

ADD_CUSTOM_TARGET(debug
        COMMAND ${CMAKE_COMMAND} -DFAKE=1 -DCMAKE_BUILD_TYPE=Debug ${CMAKE_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target all
        COMMENT "Switch CMAKE_BUILD_TYPE to Debug"
    )

ADD_CUSTOM_TARGET(release
        COMMAND ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE=Release ${CMAKE_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target all
        COMMENT "Switch CMAKE_BUILD_TYPE to Release"
    )
