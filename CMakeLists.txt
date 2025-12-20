cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
project(NukeCGDenoiser)

# Set the C++ Standard for the project (C++17 in this case)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# RPATH Configuration - Make plugin portable
# ============================================================================
# Don't set RPATH - rely on Nuke's LD_LIBRARY_PATH environment
# This makes the plugin work regardless of where Nuke is installed
set(CMAKE_SKIP_BUILD_RPATH TRUE)
set(CMAKE_SKIP_INSTALL_RPATH TRUE)

# Allow global toolchain to be set using GLOBAL_TOOLCHAIN env
if(DEFINED ENV{GLOBAL_TOOLCHAIN})
    message("Set toolchain $ENV{GLOBAL_TOOLCHAIN}")
    set(CMAKE_TOOLCHAIN_FILE $ENV{GLOBAL_TOOLCHAIN})
endif()

# Add custom CMake module path
set(CMAKE_MODULE_PATH "CMake;${CMAKE_MODULE_PATH};${CMAKE_SOURCE_DIR}/cmake/Modules")
set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

# Set Nuke version and directory (allow override via -DNUKE_VERSION)
if(NOT DEFINED NUKE_VERSION)
    set(NUKE_VERSION "16.0v6")
endif()
set(NUKE_INSTALL_DIR "/opt/Nuke${NUKE_VERSION}")
set(NUKE_INCLUDE_DIR "${NUKE_INSTALL_DIR}/include")
if(EXISTS ${NUKE_INCLUDE_DIR})
    include_directories(${NUKE_INCLUDE_DIR})
    message(STATUS "Include directory found: ${NUKE_INCLUDE_DIR}")
else()
    message(FATAL_ERROR "Include directory not found: ${NUKE_INCLUDE_DIR}")
endif()

# ABI compatibility with Nuke
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_GLIBCXX_USE_CXX11_ABI=0")

# ============================================================================
# OpenImageDenoise Configuration (Pure Static - CPU Only)
# ============================================================================
if(NOT DEFINED OIDN_ROOT)
    if(DEFINED ENV{OIDN_ROOT})
        set(OIDN_ROOT $ENV{OIDN_ROOT})
    else()
        message(FATAL_ERROR "OIDN_ROOT not set. Please specify: cmake -DOIDN_ROOT=/path/to/oidn-install ..")
    endif()
endif()

message(STATUS "OIDN_ROOT: ${OIDN_ROOT}")

# Determine lib directory (could be lib or lib64)
if(EXISTS "${OIDN_ROOT}/lib64")
    set(OIDN_LIB_DIR "${OIDN_ROOT}/lib64")
elseif(EXISTS "${OIDN_ROOT}/lib")
    set(OIDN_LIB_DIR "${OIDN_ROOT}/lib")
else()
    message(FATAL_ERROR "Could not find OIDN library directory in ${OIDN_ROOT}")
endif()

# Find OIDN include directory
find_path(OIDN_INCLUDE_DIR
    NAMES OpenImageDenoise/oidn.hpp
    PATHS ${OIDN_ROOT}/include
    NO_DEFAULT_PATH
    REQUIRED
)

# Find ALL OIDN static libraries explicitly
find_library(OIDN_STATIC_LIB
    NAMES OpenImageDenoise libOpenImageDenoise.a
    PATHS ${OIDN_LIB_DIR}
    NO_DEFAULT_PATH
    REQUIRED
)

find_library(OIDN_CORE_LIB
    NAMES OpenImageDenoise_core libOpenImageDenoise_core.a
    PATHS ${OIDN_LIB_DIR}
    NO_DEFAULT_PATH
    REQUIRED
)

find_library(OIDN_CPU_DEVICE_LIB
    NAMES OpenImageDenoise_device_cpu libOpenImageDenoise_device_cpu.a
    PATHS ${OIDN_LIB_DIR}
    NO_DEFAULT_PATH
    REQUIRED
)

# Find Threads (required by TBB)
find_package(Threads REQUIRED)

# Find TBB (required by OIDN)
find_package(TBB REQUIRED)

message(STATUS "========================================")
message(STATUS "OIDN Configuration (PURE STATIC):")
message(STATUS "  Include: ${OIDN_INCLUDE_DIR}")
message(STATUS "  Main static lib: ${OIDN_STATIC_LIB}")
message(STATUS "  Core static lib: ${OIDN_CORE_LIB}")
message(STATUS "  CPU device lib: ${OIDN_CPU_DEVICE_LIB}")
message(STATUS "  TBB found: ${TBB_FOUND}")
message(STATUS "========================================")

include_directories(${OIDN_INCLUDE_DIR})

# ============================================================================
# Nuke Configuration
# ============================================================================
message(STATUS "NUKE_VERSION: ${NUKE_VERSION}")
message(STATUS "NUKE_INSTALL_DIR: ${NUKE_INSTALL_DIR}")
message(STATUS "Install Prefix: ${CMAKE_INSTALL_PREFIX}")

# Find the Nuke core library
find_library(NUKE_LIBRARIES DDImage PATHS "${NUKE_INSTALL_DIR}" NO_DEFAULT_PATH)
if(NOT NUKE_LIBRARIES)
    message(FATAL_ERROR "Could not find the Nuke library (libDDImage.so).")
else()
    message(STATUS "Nuke Libraries found: ${NUKE_LIBRARIES}")
endif()

# ============================================================================
# Build Denoiser Plugin
# ============================================================================
set(DENOISER_SRC "${CMAKE_SOURCE_DIR}/src/denoiser.cpp")
if(EXISTS ${DENOISER_SRC})
    message(STATUS "Found Denoiser.cpp at ${DENOISER_SRC}")
else()
    message(FATAL_ERROR "Denoiser.cpp not found at ${DENOISER_SRC}")
endif()

# Set the library output directory
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Create the shared library plugin
add_library(Denoiser SHARED src/denoiser.cpp)

# Remove 'lib' prefix (Nuke convention)
set_target_properties(Denoiser PROPERTIES PREFIX "")

# CRITICAL: Link OIDN libraries in correct order with whole-archive
# This ensures all symbols are included from static libraries
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # For GCC/Clang: Use -Wl,--whole-archive to force inclusion of all symbols
    target_link_libraries(Denoiser PRIVATE
        ${NUKE_LIBRARIES}
        -Wl,--whole-archive
        ${OIDN_STATIC_LIB}
        ${OIDN_CORE_LIB}
        ${OIDN_CPU_DEVICE_LIB}
        -Wl,--no-whole-archive
        TBB::tbb
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )
else()
    # Fallback for other compilers
    target_link_libraries(Denoiser PRIVATE
        ${NUKE_LIBRARIES}
        ${OIDN_STATIC_LIB}
        ${OIDN_CORE_LIB}
        ${OIDN_CPU_DEVICE_LIB}
        TBB::tbb
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )
endif()

# No RPATH needed - Nuke's environment provides library paths
# This makes the plugin portable across different Nuke installations

# Installation
install(TARGETS Denoiser DESTINATION ${CMAKE_INSTALL_PREFIX})

message(STATUS "========================================")
message(STATUS "Build Configuration Summary:")
message(STATUS "  Plugin: Denoiser.so")
message(STATUS "  Nuke Version: ${NUKE_VERSION}")
message(STATUS "  Nuke Install: ${NUKE_INSTALL_DIR}")
message(STATUS "  OIDN: PURE STATIC (CPU-only)")
message(STATUS "  Linking: --whole-archive (all symbols)")
message(STATUS "  RPATH: Not set (portable - uses Nuke's environment)")
message(STATUS "  Output: ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message(STATUS "  Install: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "========================================")
