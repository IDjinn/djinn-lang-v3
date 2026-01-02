cmake_minimum_required(VERSION 3.26)

cmake_policy(SET CMP0091 NEW)

project(djinn LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

file(GLOB_RECURSE DJINN_SOURCES
        lexer/*.cpp
        parser/*.cpp
        generator/*.cpp
        generator/expressions/*.cpp
        generator/statements/*.cpp
        diagnostics/*.cpp
)

file(GLOB_RECURSE DJINN_HEADERS
        lexer/*.h
        parser/*.h
        generator/*.h
        diagnostics/*.h
        parser/ast/*.h
)

include_directories(${CMAKE_SOURCE_DIR}/include)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

llvm_map_components_to_libnames(LLVM_LIBS
        Core
        Support
        IRReader
        ExecutionEngine
        MCJIT
        native
        Passes
)

add_library(djinn_lib STATIC
        ${DJINN_SOURCES}
        ${DJINN_HEADERS}
        DjinnCompiler.cpp
        DjinnCompiler.h
)

target_link_libraries(djinn_lib PRIVATE ${LLVM_LIBS})
target_compile_features(djinn_lib PUBLIC cxx_std_23)

add_executable(djinn main.cpp)
target_link_libraries(djinn PRIVATE djinn_lib)

# ========================

add_subdirectory(external/googletest)

enable_testing()

file(GLOB_RECURSE TEST_SOURCES
        tests/*.cpp
)

add_executable(tests ${TEST_SOURCES})

target_link_libraries(tests PRIVATE
        gtest_main
        djinn_lib
)

target_compile_features(tests PRIVATE cxx_std_23)

include(GoogleTest)
gtest_discover_tests(tests)
