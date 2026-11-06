add_executable(unit_test test.cpp)

target_include_directories(unit_test PRIVATE ..)

# shared_with_test brings the warnings, FAKE, fmt and threads with it
target_link_libraries(unit_test PRIVATE shared_with_test)

add_test(NAME test_all COMMAND unit_test)

# Tests of the Qt user interface (run on Qt's offscreen platform, so no
# windows appear)
find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(ui_test
	test_ui.cpp
	../view_qt.cpp
	../qt_dialog.cpp
	../controller.cpp
	../configreader.cpp
)
target_include_directories(ui_test PRIVATE ..)
target_link_libraries(ui_test PRIVATE shared_with_test Qt6::Widgets Qt6::Test)

# Run from the source root, where the font and images are
add_test(NAME ui COMMAND ui_test WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
