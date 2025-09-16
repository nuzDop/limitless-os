# LimitlessOS Root CMakeLists.txt
#
# This file manages the top-level build configuration and includes
# all subsystem components.

cmake_minimum_required(VERSION 3.16)

project(LimitlessOS C ASM)

# --- Global Build Configuration ---
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Set the cross-compiler toolchain
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_PREFIX "$ENV{HOME}/opt/cross/bin/x86_64-elf-")
set(CMAKE_C_COMPILER "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_PREFIX}as")
set(CMAKE_AR "${TOOLCHAIN_PREFIX}ar" CACHE FILEPATH "Archiver")

# Global compiler flags for kernel development
add_compile_options(
    -Wall
    -Wextra
    -Werror
    -pedantic
    -ffreestanding
    -fno-stack-protector
    -mno-red-zone
    -mcmodel=kernel
    -mno-mmx
    -mno-sse
    -mno-sse2
)

# --- Add Subsystems ---
# Each subsystem is a self-contained module with its own CMakeLists.txt

add_subdirectory(continuum)
add_subdirectory(harmony)
add_subdirectory(manifold)
# ... add other kernel-level components here

add_subdirectory(apps)
add_subdirectory(genesis)

# --- Final Output ---
message(STATUS "LimitlessOS configuration complete.")
message(STATUS "Run 'make' to build all components.")
