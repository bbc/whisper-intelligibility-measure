enable_testing()

add_executable(plugin-tests
    tests.cpp)

set_target_properties(plugin-tests PROPERTIES FOLDER Tests)

target_include_directories(plugin-tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${JUCE_SOURCE_DIR}/modules
        ${GOOGLETEST_SOURCE_DIR}/googletest/include)

target_link_libraries(plugin-tests
    PRIVATE
        ${PROJECT_NAME}
        GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(plugin-tests DISCOVERY_MODE PRE_TEST)