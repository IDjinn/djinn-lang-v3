cmake_minimum_required(VERSION 4.0)

cmake_policy(SET CMP0091 NEW)

project(djinn)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC: usar runtime library estática (consistência com LLVM)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Habilitar testes
enable_testing()


file(GLOB_RECURSE SOURCES
        "lexer/*.cpp"
        "lexer/*.h"
        "parser/*.cpp"
        "parser/*.h"
        "codegen/*.cpp"
        "codegen/*.h"
        "main.cpp"
)

set(GLAD_SOURCES src/glad.c)

set(OPT_FLAGS
        -O3 -march=native -mtune=native -flto
        -fstack-protector-strong
        -fstack-clash-protection
        -fcf-protection
)


set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${OPT_FLAGS}")
include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/include)

# LLVM
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

add_executable(djinn ${SOURCES})
target_compile_options(djinn PRIVATE ${COMMON_COMPILE_OPTIONS})

llvm_map_components_to_libnames(llvm_libs
        Core
        Support
        IRReader
        ExecutionEngine
        MCJIT
        native
        Passes
)
target_link_libraries(djinn ${llvm_libs})

