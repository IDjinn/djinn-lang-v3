cmake_minimum_required(VERSION 3.26)

cmake_policy(SET CMP0091 NEW)

project(djinn LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (MSVC)
    set(DJINN_WARNINGS
            /W4
            /permissive-
            /w14242
            /w14254
            /w14263
            /w14265
            /w14287
            /w14296
            /w14311
            /w14545
            /w14546
            /w14547
            /w14549
            /w14555
            /w14619
            /w14640
            /w14826
            /w14905
            /w14906
            /w14928
    )
else ()
    set(DJINN_WARNINGS
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Wunused
            -Woverloaded-virtual
            -Wconversion
            -Wsign-conversion
            -Wnull-dereference
            -Wdouble-promotion
            -Wformat=2
            -Wimplicit-fallthrough
    )
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        list(APPEND DJINN_WARNINGS
                -Wmisleading-indentation
                -Wduplicated-cond
                -Wduplicated-branches
                -Wlogical-op
                -Wuseless-cast
        )
    endif ()
endif ()

if (MSVC)
    # MSVC: only ASan is supported
    # Remove /RTC1 (Runtime Checks) — incompatible with ASan
    string(REGEX REPLACE "/RTC[^ ]*" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    add_compile_options($<$<CONFIG:Debug>:/fsanitize=address>)
    add_link_options($<$<CONFIG:Debug>:/INCREMENTAL:NO>)
    # Disable STL container annotations — LLVM libs were built without ASan
    add_compile_definitions($<$<CONFIG:Debug>:_DISABLE_STRING_ANNOTATION>)
    add_compile_definitions($<$<CONFIG:Debug>:_DISABLE_VECTOR_ANNOTATION>)
else ()
    # GCC/Clang: ASan + UBSan + LeakSan (compatible combo)
    add_compile_options(
            $<$<CONFIG:Debug>:-fsanitize=address,undefined,leak>
            $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
            $<$<CONFIG:Debug>:-fno-optimize-sibling-calls>
            $<$<CONFIG:Debug>:-fno-sanitize-recover=all>
    )
    add_link_options(
            $<$<CONFIG:Debug>:-fsanitize=address,undefined,leak>
    )
endif ()

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
        binder/ownership/*.cpp
        evaluator/*.cpp
        utils/*.cpp
        lib/*.cpp
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
        binder/ownership/*.h
        visitor/*.h
        evaluator/*.h
        lib/*.h
)


message(STATUS "LIBXML2_INCLUDE_DIR = ${LIBXML2_INCLUDE_DIR}")
message(STATUS "LIBXML2_LIBRARY = ${LIBXML2_LIBRARY}")

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Resolve clang path from the same LLVM installation
find_program(LLVM_CLANG_PATH clang PATHS "${LLVM_TOOLS_BINARY_DIR}" NO_DEFAULT_PATH)
if (NOT LLVM_CLANG_PATH)
    find_program(LLVM_CLANG_PATH clang)
endif ()
message(STATUS "Using clang: ${LLVM_CLANG_PATH}")
add_compile_definitions(DJINN_CLANG_PATH="${LLVM_CLANG_PATH}")

# LLVM includes added per-target as SYSTEM (see below)
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

# Suppress LLVM-related warnings on MSVC
# /external:anglebrackets + /external:W0 treats all <> includes as external with no warnings
if (MSVC)
    add_compile_options(/external:anglebrackets /external:W0 /wd4624 /wd4267 /wd4244 /wd4996 /wd4146 /wd4702)
endif ()

llvm_map_components_to_libnames(LLVM_LIBS
        Core
        Support
        IRReader
        ExecutionEngine
        MCJIT
        native
        Passes
        Linker
        BitWriter
        BitReader
)

option(DJINN_DISABLE_BINDER_ASSERTS "Disable binder debug assertions" ON)

add_library(djinn_lib STATIC
        ${DJINN_SOURCES}
        ${DJINN_HEADERS}
        DjinnCompiler.cpp
        DjinnCompiler.h
)

if (DJINN_DISABLE_BINDER_ASSERTS)
    target_compile_definitions(djinn_lib PUBLIC DJINN_TESTING)
endif ()

target_compile_definitions(djinn_lib PUBLIC DJINN_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(djinn_lib PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_include_directories(djinn_lib SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS})
target_compile_options(djinn_lib PRIVATE ${DJINN_WARNINGS})
target_link_libraries(djinn_lib PRIVATE ${LLVM_LIBS})
target_link_libraries(djinn_lib PRIVATE ${LIBXML2_LIBRARY})
target_link_libraries(djinn_lib PUBLIC ryml::ryml)
target_link_libraries(djinn_lib PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(djinn_lib PUBLIC cxx_std_23)

# std::stacktrace requires the backtrace support library on GCC >= 14 / Clang with libstdc++
if (UNIX AND NOT APPLE)
    find_library(STDCXX_LIBBACKTRACE NAMES stdc++_libbacktrace)
    if (STDCXX_LIBBACKTRACE)
        target_link_libraries(djinn_lib PUBLIC ${STDCXX_LIBBACKTRACE})
    endif ()
endif ()

add_executable(djinn main.cpp)
target_compile_options(djinn PRIVATE ${DJINN_WARNINGS})
target_link_libraries(djinn PRIVATE djinn_lib)

include(FetchContent)

FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
)

FetchContent_Declare(
        rapidyaml
        GIT_REPOSITORY https://github.com/biojppm/rapidyaml.git
        GIT_TAG v0.7.2
        GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(nlohmann_json rapidyaml)

add_executable(djinn-lsp
        lsp/main.cpp
        lsp/LspServer.cpp
        lsp/JsonRpc.cpp
)

target_compile_options(djinn-lsp PRIVATE ${DJINN_WARNINGS})
target_link_libraries(djinn-lsp PRIVATE
        djinn_lib
        nlohmann_json::nlohmann_json
)

if (WIN32)
    target_link_libraries(djinn-lsp PRIVATE ws2_32)
endif ()

target_compile_features(djinn-lsp PRIVATE cxx_std_23)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

if (EXISTS "${CMAKE_SOURCE_DIR}/external/googletest/CMakeLists.txt")
    message(STATUS "Using GoogleTest from submodule")
    add_subdirectory(external/googletest)
else ()
    message(STATUS "Downloading GoogleTest via FetchContent")
    FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif ()

enable_testing()

file(GLOB_RECURSE TEST_SOURCES
        tests/*.cpp
)

add_executable(tests ${TEST_SOURCES})

target_compile_options(tests PRIVATE ${DJINN_WARNINGS})
target_link_libraries(tests PRIVATE
        gtest_main
        djinn_lib
)

target_compile_features(tests PRIVATE cxx_std_23)

include(GoogleTest)
gtest_discover_tests(tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_custom_target(clean_djinn_build
        COMMAND ${CMAKE_COMMAND} -E rm -rf "$ENV{TEMP}/djinn_build"
        COMMENT "Cleaning djinn_build temp directory"
)
add_dependencies(tests clean_djinn_build)

# ========================
# Runtime
# ========================
if (MSVC)
    set(CMAKE_C_STANDARD 17)
else ()
    set(CMAKE_C_STANDARD 23)
endif ()
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
add_library(djinn_runtime STATIC
        runtime/djinn_runtime.c
        runtime/djinn_runtime.h
        runtime/logger.h
        runtime/logger.c
)
target_include_directories(djinn_runtime PUBLIC "${CMAKE_SOURCE_DIR}/runtime")
set_target_properties(djinn_runtime PROPERTIES LINKER_LANGUAGE C)

add_custom_command(TARGET djinn_lib POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/runtime"
        "${CMAKE_BINARY_DIR}/runtime"
        COMMENT "Copying runtime to build directory"
)
