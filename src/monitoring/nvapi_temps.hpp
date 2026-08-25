#pragma once

#include <optional>

namespace xn {

// RTX 50-series memory junction via undocumented NvAPI_GPU_GetThermalSensors.
// NVML field 82 / nvidia-smi temperature.memory are N/A on Blackwell consumer.
std::optional<int> read_nvapi_memory_junction_c(int device_index);

}  // namespace xn
