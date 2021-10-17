project (test)

add_executable(test test.cpp)

target_compile_options(test PRIVATE -std=c++17 -g -Wall -Wextra -Werror -Wpedantic)

target_include_directories(test PRIVATE ..)

target_link_libraries(test PRIVATE shared_with_test fmt)
