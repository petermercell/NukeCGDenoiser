cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
project(NukeCGDenoiser)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Allow global toolchain to be set using GLOBAL_TOOLCHAIN env
if(DEFINED ENV{GLOBAL_TOOLCHAIN})
    message("Set toolchain $ENV{GLOBAL_TOOLCHAIN}")
    set(CMAKE_TOOLCHAIN_FILE $ENV{GLOBAL_TOOLCHAIN})
endif()

set(CMAKE_MODULE_PATH "CMake;${CMAKE_MODULE_PATH};${CMAKE_SOURCE_DIR}/cmake/Modules")
set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

# ============================================================================
# Nuke Configuration
# ============================================================================
if(NOT DEFINED NUKE_VERSION)
    set(NUKE_VERSION "17.0v1")
endif()
set(NUKE_INSTALL_DIR "/opt/Nuke${NUKE_VERSION}")
set(NUKE_INCLUDE_DIR "${NUKE_INSTALL_DIR}/include")
if(EXISTS ${NUKE_INCLUDE_DIR})
    include_directories(${NUKE_INCLUDE_DIR})
    message(STATUS "Nuke include directory: ${NUKE_INCLUDE_DIR}")
else()
    message(FATAL_ERROR "Include directory not found: ${NUKE_INCLUDE_DIR}")
endif()

find_library(NUKE_LIBRARIES DDImage PATHS "${NUKE_INSTALL_DIR}" NO_DEFAULT_PATH)
if(NOT NUKE_LIBRARIES)
    message(FATAL_ERROR "Could not find libDDImage.so in ${NUKE_INSTALL_DIR}")
else()
    message(STATUS "Nuke Libraries: ${NUKE_LIBRARIES}")
endif()

# ============================================================================
# OpenImageDenoise Configuration (Dynamic - GPU + CPU)
# ============================================================================
if(NOT DEFINED OIDN_ROOT)
    if(DEFINED ENV{OIDN_ROOT})
        set(OIDN_ROOT $ENV{OIDN_ROOT})
    else()
        message(FATAL_ERROR "OIDN_ROOT not set. Please specify: cmake -DOIDN_ROOT=/path/to/oidn ..")
    endif()
endif()

message(STATUS "OIDN_ROOT: ${OIDN_ROOT}")

# Determine lib directory
if(EXISTS "${OIDN_ROOT}/lib64")
    set(OIDN_LIB_DIR "${OIDN_ROOT}/lib64")
elseif(EXISTS "${OIDN_ROOT}/lib")
    set(OIDN_LIB_DIR "${OIDN_ROOT}/lib")
else()
    message(FATAL_ERROR "Could not find OIDN library directory in ${OIDN_ROOT}")
endif()

# Find OIDN include
find_path(OIDN_INCLUDE_DIR
    NAMES OpenImageDenoise/oidn.hpp
    PATHS ${OIDN_ROOT}/include
    NO_DEFAULT_PATH
    REQUIRED
)

# Find the SHARED library (libOpenImageDenoise.so)
find_library(OIDN_SHARED_LIB
    NAMES OpenImageDenoise
    PATHS ${OIDN_LIB_DIR}
    NO_DEFAULT_PATH
    REQUIRED
)

message(STATUS "========================================")
message(STATUS "OIDN Configuration (DYNAMIC - GPU+CPU):")
message(STATUS "  Include: ${OIDN_INCLUDE_DIR}")
message(STATUS "  Shared lib: ${OIDN_SHARED_LIB}")
message(STATUS "  Lib dir: ${OIDN_LIB_DIR}")
message(STATUS "========================================")

include_directories(${OIDN_INCLUDE_DIR})

# ============================================================================
# RPATH Configuration - Find OIDN shared libs at runtime
# ============================================================================
# Embed RPATH so the plugin can find OIDN shared libraries
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "${OIDN_LIB_DIR}")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# ============================================================================
# Build Denoiser Plugin
# ============================================================================
set(DENOISER_SRC "${CMAKE_SOURCE_DIR}/src/denoiser.cpp")
if(EXISTS ${DENOISER_SRC})
    message(STATUS "Found denoiser.cpp at ${DENOISER_SRC}")
else()
    message(FATAL_ERROR "denoiser.cpp not found at ${DENOISER_SRC}")
endif()

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_library(Denoiser SHARED src/denoiser.cpp)
set_target_properties(Denoiser PROPERTIES PREFIX "")

# Set RPATH on the built library directly
set_target_properties(Denoiser PROPERTIES
    BUILD_RPATH "${OIDN_LIB_DIR}"
    INSTALL_RPATH "${OIDN_LIB_DIR}"
)

# Dynamic linking - just link the main shared library
# GPU device libs (CUDA/SYCL/HIP) are loaded automatically at runtime by OIDN
target_link_libraries(Denoiser PRIVATE
    ${NUKE_LIBRARIES}
    ${OIDN_SHARED_LIB}
    ${CMAKE_DL_LIBS}
)

install(TARGETS Denoiser DESTINATION ${CMAKE_INSTALL_PREFIX})

message(STATUS "========================================")
message(STATUS "Build Configuration Summary:")
message(STATUS "  Plugin: Denoiser.so")
message(STATUS "  Nuke Version: ${NUKE_VERSION}")
message(STATUS "  OIDN: DYNAMIC (GPU + CPU)")
message(STATUS "  OIDN RPATH: ${OIDN_LIB_DIR}")
message(STATUS "  Output: ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message(STATUS "========================================")
