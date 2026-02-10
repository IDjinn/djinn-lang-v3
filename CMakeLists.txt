cmake_minimum_required(VERSION 3.26)

cmake_policy(SET CMP0091 NEW)

project(djinn LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ========================
# Warnings & Best Practices
# ========================

if (MSVC)
    set(DJINN_WARNINGS
            /W4                 # High warning level
            /permissive-        # Strict standards conformance
            /w14242             # Narrowing conversion
            /w14254             # Operator conversion larger bitfield
            /w14263             # Member function does not override base virtual
            /w14265             # Class has virtual functions but no virtual destructor
            /w14287             # Unsigned/negative constant mismatch
            /w14296             # Expression is always false
            /w14311             # Pointer truncation
            /w14545             # Expression before comma evaluates to function
            /w14546             # Function call before comma missing argument list
            /w14547             # Operator before comma has no effect
            /w14549             # Operator before comma has no effect
            /w14555             # Expression has no effect
            /w14619             # Pragma warning: nonexistent warning number
            /w14640             # Thread-unsafe static local initialization
            /w14826             # Conversion is sign-extended
            /w14905             # Wide string literal cast to LPSTR
            /w14906             # String literal cast to LPWSTR
            /w14928             # Illegal copy-initialization
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

# ========================
# Sanitizers (Debug only)
# ========================

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
)

include_directories(${CMAKE_SOURCE_DIR}/include)

# Define source root for logger relative paths
add_compile_definitions(SOURCE_ROOT="${CMAKE_SOURCE_DIR}/")

message(STATUS "LIBXML2_INCLUDE_DIR = ${LIBXML2_INCLUDE_DIR}")
message(STATUS "LIBXML2_LIBRARY = ${LIBXML2_LIBRARY}")

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

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
)

# Option to disable binder assertions (needed for testing error cases)
# ON by default so tests work; set to OFF for strict development checks
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

target_include_directories(djinn_lib SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS})
target_compile_options(djinn_lib PRIVATE ${DJINN_WARNINGS})
target_link_libraries(djinn_lib PRIVATE ${LLVM_LIBS})
target_link_libraries(djinn_lib PRIVATE ${LIBXML2_LIBRARY})
target_compile_features(djinn_lib PUBLIC cxx_std_23)

add_executable(djinn main.cpp)
target_compile_options(djinn PRIVATE ${DJINN_WARNINGS})
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

target_compile_options(djinn-lsp PRIVATE ${DJINN_WARNINGS})
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
# Google Test
# ========================

# Force gtest to use shared CRT to match our runtime library setting
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Try submodule first, fallback to FetchContent download
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
    # For Windows: Prevent overriding the parent project's compiler/linker settings
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
gtest_discover_tests(tests)
