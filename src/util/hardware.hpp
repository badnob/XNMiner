#pragma once

#include <string>

namespace xn {

struct HardwareProfile {
    int device_id = 0;
    int cuda_devices = 0;
    std::string gpu_name = "none";
    int sm_major = 0;
    int sm_minor = 0;
    int sm_arch = 0;  // 75, 86, 89, 90, 120, ...
    std::string family = "unknown";
    int vram_mib = 0;
    int cpu_cores = 0;
    int suggested_lanes = 1;
    int suggested_batch_m100 = 0;
    int suggested_keygen = 2;
    bool cuda_ok = false;
    std::string error;

    std::string summary() const;
};

std::string sm_family_name(int major, int minor);
HardwareProfile probe_hardware(int device_id);

}  // namespace xn
