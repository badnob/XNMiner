cmake_minimum_required(VERSION 3.18)
cmake_policy(SET CMP0057 NEW)
project(xnminer_cuda LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# Prefer the GPU in this box. build.sh passes CMAKE_CUDA_ARCHITECTURES from
# nvidia-smi (75 / 86 / 89 / 90 / 120a). Blackwell is 120a. If cmake is run
# by hand with no -D: native on CMake 3.24+, otherwise a Turing-through-Blackwell cubin.
if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24")
        set(CMAKE_CUDA_ARCHITECTURES native)
    else()
        set(CMAKE_CUDA_ARCHITECTURES "75;80;86;89;90;120a")
    fi
endif()

set(VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor")
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_executable(xnminer
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/config/settings.cpp
    ${SRC_DIR}/util/http.cpp
    ${SRC_DIR}/util/logger.cpp
    ${SRC_DIR}/util/paths.cpp
    ${SRC_DIR}/util/sha256.cpp
    ${SRC_DIR}/util/cpu_affinity.cpp
    ${SRC_DIR}/util/hardware.cpp
    ${SRC_DIR}/mining/argon2_encode.cpp
    ${SRC_DIR}/mining/block_types.cpp
    ${SRC_DIR}/mining/cuda_engine.cpp
    ${SRC_DIR}/mining/gpu_test.cpp
    ${SRC_DIR}/mining/vram_batch.cpp
    ${SRC_DIR}/efficiency/vram_policy.cpp
    ${SRC_DIR}/efficiency/thermal_policy.cpp
    ${SRC_DIR}/efficiency/gpu_power.cpp
    ${SRC_DIR}/network/difficulty.cpp
    ${SRC_DIR}/network/submitter.cpp
    ${SRC_DIR}/queue/store.cpp
    ${SRC_DIR}/queue/policy.cpp
    ${SRC_DIR}/monitoring/nvml_monitor.cpp
    ${SRC_DIR}/monitoring/nvapi_temps.cpp
    ${SRC_DIR}/monitoring/metrics.cpp
    ${SRC_DIR}/monitoring/dashboard.cpp
    ${SRC_DIR}/monitoring/woodyminer.cpp
    ${SRC_DIR}/monitoring/wallet.cpp
    ${SRC_DIR}/monitoring/xenblockscan.cpp
    ${SRC_DIR}/monitoring/timelapse.cpp
    ${SRC_DIR}/monitoring/local_stats.cpp
    ${SRC_DIR}/app/supervisor.cpp
    ${SRC_DIR}/app/instance_lock.cpp
    ${VENDOR_DIR}/argon2params.cpp
    ${VENDOR_DIR}/blake2b.cpp
    ${VENDOR_DIR}/CudaBackend.cpp
    ${VENDOR_DIR}/CudaDevice.cpp
    ${VENDOR_DIR}/kernelrunner.cu
    ${VENDOR_DIR}/hashapi/CudaHashBackend.cpp
    ${VENDOR_DIR}/hashapi/KeygenPool.cpp
    ${VENDOR_DIR}/hashapi/HashApiEncoding.cpp
    ${VENDOR_DIR}/hashapi/HashApiMatching.cpp
    ${VENDOR_DIR}/hashapi/HashApiTuning.cpp
    ${VENDOR_DIR}/hashapi/HashApiValidation.cpp
)

target_include_directories(xnminer PRIVATE
    ${SRC_DIR}
    ${VENDOR_DIR}
    ${VENDOR_DIR}/hashapi
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

find_package(CUDAToolkit REQUIRED)
find_package(Threads REQUIRED)
# Static cudart so a CUDA 13 binary runs on the image's driver-580 libcuda
# without needing libcudart.so.13 on LD_LIBRARY_PATH.
if(UNIX AND TARGET CUDA::cudart_static)
    target_link_libraries(xnminer PRIVATE CUDA::cudart_static Threads::Threads)
else()
    target_link_libraries(xnminer PRIVATE CUDA::cudart Threads::Threads)
endif()

if(WIN32)
    target_compile_definitions(xnminer PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    target_link_libraries(xnminer PRIVATE winhttp bcrypt advapi32 iphlpapi ws2_32)
else()
    find_package(CURL REQUIRED)
    target_link_libraries(xnminer PRIVATE CURL::libcurl ${CMAKE_DL_LIBS})
    target_compile_definitions(xnminer PRIVATE _GNU_SOURCE)
endif()

target_compile_options(xnminer PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:-Wno-deprecated-gpu-targets>
    $<$<COMPILE_LANGUAGE:CUDA>:-O3>
    $<$<COMPILE_LANGUAGE:CUDA>:--extra-device-vectorization>
    $<$<COMPILE_LANGUAGE:CUDA>:-Xptxas=-O3,-dlcm=ca>
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:/O2>
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-O3>
)

# Kernel hint only when this cubin includes Blackwell SASS.
string(REPLACE "," ";" _xn_cuda_arches "${CMAKE_CUDA_ARCHITECTURES}")
if("120" IN_LIST _xn_cuda_arches OR "120a" IN_LIST _xn_cuda_arches)
    target_compile_definitions(xnminer PRIVATE XN_BLACKWELL_BUILD=1)
endif()

set_target_properties(xnminer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    CUDA_SEPARABLE_COMPILATION OFF
)
