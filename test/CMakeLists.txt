add_executable(unit_test test.cpp)

target_include_directories(unit_test PRIVATE ..)

# shared_with_test brings the warnings, FAKE, fmt and threads with it
target_link_libraries(unit_test PRIVATE shared_with_test)

add_test(NAME test_all COMMAND unit_test)
