project (unit_tests)

add_executable(unit_test test.cpp)

target_compile_options(unit_test PRIVATE -std=c++23 -g -Wall -Wextra -Werror -Wpedantic)

target_include_directories(unit_test PRIVATE ..)

target_link_libraries(unit_test PRIVATE shared_with_test fmt pthread)

# Raspberry Pi 32-bit needs atomic library:
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "armv7l")
    target_link_libraries(shared_with_test PRIVATE atomic)
endif()

add_test(test_all unit_test)
