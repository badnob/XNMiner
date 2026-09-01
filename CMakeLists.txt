cmake_minimum_required(VERSION 3.20)
project(XN_Miner_Project)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Standard components
find_package(Threads REQUIRED)

# OpenCL detection
if(OPENCL_RUNTIME)
    find_package(OpenCL REQUIRED)
endif()

# CUDA/CUDART detection
# Using CUDA module for broader compatibility with Windows/NVIDIA setups
find_package(CUDA REQUIRED)
find_package(CUDNN) # Optional

set(XN_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src)
set(XN_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

# Architecture Selection
set(ARCH_LIST \"75;80;86;89;90;120a\")
set(CMAKE_CUDA_ARCHITECTURES ${ARCH_LIST})

# Include Paths
include_directories(
    ${XN_INCLUDE_DIR}
    ${XN_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/vendor
    ${CUDA_INCLUDE_DIRS}
)

# Linker settings
link_directories(${CUDA_LIBRARY_PATH})
