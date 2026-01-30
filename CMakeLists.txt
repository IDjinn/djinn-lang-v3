cmake_minimum_required(VERSION 3.26)

cmake_policy(SET CMP0091 NEW)

project(djinn LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif ()

file(GLOB_RECURSE DJINN_SOURCES
        lexer/*.cpp
        parser/*.cpp
        generator/*.cpp
        generator/expressions/*.cpp
        generator/statements/*.cpp
        generator/visitors/*.cpp
        diagnostics/*.cpp
        binder/*.cpp
        binder/visitors/*.cpp
        binder/handlers/*.cpp
        binder/validation/*.cpp
        binder/scope/*.cpp
)

file(GLOB_RECURSE DJINN_HEADERS
        lexer/*.h
        parser/*.h
        generator/*.h
        generator/visitors/*.h
        diagnostics/*.h
        parser/ast/*.h
        binder/*.h
        binder/visitors/*.h
        binder/handlers/*.h
        binder/validation/*.h
        binder/scope/*.h
        visitor/*.h
)

include_directories(${CMAKE_SOURCE_DIR}/include)

# Define source root for logger relative paths
add_compile_definitions(SOURCE_ROOT="${CMAKE_SOURCE_DIR}/")

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Mark LLVM includes as SYSTEM to suppress warnings
include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

# Suppress LLVM-related warnings on MSVC
if (MSVC)
    add_compile_options(/wd4624 /wd4267 /wd4244 /wd4996 /wd4146)
endif ()

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
# LSP Server
# ========================

# Fetch nlohmann/json
include(FetchContent)
FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

add_executable(djinn-lsp
        lsp/main.cpp
        lsp/LspServer.cpp
        lsp/JsonRpc.cpp
)

target_link_libraries(djinn-lsp PRIVATE
        djinn_lib
        nlohmann_json::nlohmann_json
)

# Link Windows socket library
if (WIN32)
    target_link_libraries(djinn-lsp PRIVATE ws2_32)
endif ()

target_compile_features(djinn-lsp PRIVATE cxx_std_23)

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
