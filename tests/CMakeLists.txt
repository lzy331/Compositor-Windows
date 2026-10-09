add_executable(test_swiftjson test_swiftjson.cpp)
target_link_libraries(test_swiftjson PRIVATE compositor_core)
add_test(NAME swiftjson COMMAND test_swiftjson)

add_executable(test_manifestcodec test_manifestcodec.cpp)
target_link_libraries(test_manifestcodec PRIVATE compositor_core)
add_test(NAME manifestcodec COMMAND test_manifestcodec)

add_executable(test_hierarchy test_hierarchy.cpp)
target_link_libraries(test_hierarchy PRIVATE compositor_core)
add_test(NAME hierarchy COMMAND test_hierarchy)

add_executable(test_validation test_validation.cpp)
target_link_libraries(test_validation PRIVATE compositor_core)
add_test(NAME validation COMMAND test_validation)

add_executable(test_projectstore test_projectstore.cpp)
target_link_libraries(test_projectstore PRIVATE compositor_io)
add_test(NAME projectstore COMMAND test_projectstore)

add_executable(test_renderer test_renderer.cpp)
target_link_libraries(test_renderer PRIVATE compositor_render)
add_test(NAME renderer COMMAND test_renderer)

add_executable(test_ui test_ui.cpp)
target_link_libraries(test_ui PRIVATE compositor_ui)
add_test(NAME ui COMMAND test_ui)
