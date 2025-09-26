cmake_minimum_required(VERSION 3.22)

project(WhisperIntelligibilityMeasure)
set(CMAKE_CXX_STANDARD 20)

# Dependencies

set(DEPS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps)
set(CPM_DOWNLOAD_LOCATION ${DEPS_DIR}/cpm)
include(cmake_modules/cpm.cmake)# Downloads CPM if not present

CPMAddPackage(
    NAME JUCE
    GIT_TAG 8.0.6
    VERSION 8.0.6
    GITHUB_REPOSITORY juce-framework/JUCE
    SOURCE_DIR ${DEPS_DIR}/juce
    OPTIONS
        "JUCE_COPY_PLUGIN_AFTER_BUILD ON"
)

CPMAddPackage(
  NAME libzmq
  GITHUB_REPOSITORY zeromq/libzmq
  GIT_TAG v4.3.4
  SOURCE_DIR ${DEPS_DIR}/libzmq
  OPTIONS
        "ENABLE_DRAFTS OFF"
        "ENABLE_WS OFF"
        "WITH_DOCS OFF"
        "BUILD_SHARED OFF"
        "BUILD_STATIC ON"
        "WITH_PERF_TOOL OFF"
        "ZMQ_BUILD_TESTS OFF"
        "BUILD_TESTS OFF"
        "ENABLE_CPACK OFF"
        "ENABLE_CLANG OFF"
)

CPMAddPackage(
    NAME cppzmq
    GIT_TAG v4.10.0
    GITHUB_REPOSITORY zeromq/cppzmq
    SOURCE_DIR ${DEPS_DIR}/cppzmq
    OPTIONS
        "CPPZMQ_BUILD_TESTS OFF"
)

CPMAddPackage(
    NAME GOOGLETEST
    GITHUB_REPOSITORY google/googletest
    GIT_TAG v1.13.0
    VERSION 1.13.0
    SOURCE_DIR ${DEPS_DIR}/googletest
    OPTIONS
        "INSTALL_GTEST OFF"
        "gtest_force_shared_crt ON"
)
set_target_properties(
    gmock
    gmock_main
    gtest
    gtest_main
    libzmq-static
    PROPERTIES FOLDER Dependencies)

# Actual Lib & Plugin (+tests)

enable_testing()
add_subdirectory(plugin)