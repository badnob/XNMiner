#include "util/hardware.hpp"

#include "mining/vram_batch.hpp"
#include "util/cpu_affinity.hpp"

#include <cuda_runtime.h>

#include <algorithm>
#include <sstream>

namespace xn {

std::string sm_family_name(int major, int minor) {
    const int arch = major * 10 + minor;
    if (major >= 12 || arch == 100 || arch == 101 || arch == 103) return "Blackwell";
    if (arch == 90) return "Hopper";
    if (arch == 89) return "Ada";
    if (arch == 87) return "Orin";
    if (arch == 86 || arch == 80) return "Ampere";
    if (arch == 75) return "Turing";
    if (arch == 70 || arch == 72) return "Volta";
    if (major == 6) return "Pascal";
    if (major == 5) return "Maxwell";
    return "CUDA";
}

static int estimate_batch_m100(int vram_mib, int lanes) {
    if (vram_mib <= 0) return 0;
    if (lanes < 1) lanes = 1;
    int budget_mib = static_cast<int>(vram_mib * 80 / 100) - 256;
    if (budget_mib < 1) budget_mib = std::max(1, vram_mib / 2);
    const uint64_t per_lane = static_cast<uint64_t>(std::max(1, budget_mib / lanes)) * 1024ULL * 1024ULL;
    return select_batch_size(per_lane, 100, 0, true);
}

std::string HardwareProfile::summary() const {
    std::ostringstream oss;
    oss << "CPU " << cpu_cores << " cores";
    if (cuda_ok) {
        oss << " — GPU" << device_id << " " << gpu_name << " sm_" << sm_arch << " (" << family
            << ") " << vram_mib << " MiB → auto " << suggested_lanes << " lane"
            << (suggested_lanes == 1 ? "" : "s") << " x ~" << suggested_batch_m100
            << " @ m=100, keygen " << suggested_keygen;
        if (cuda_devices > 1) oss << " [" << cuda_devices << " CUDA devices]";
    } else if (!error.empty()) {
        oss << " — CUDA: " << error;
    } else {
        oss << " — no CUDA GPU";
    }
    return oss.str();
}

HardwareProfile probe_hardware(int device_id) {
    HardwareProfile p;
    p.device_id = std::max(0, device_id);
    p.cpu_cores = cpu::online_count();
    p.suggested_keygen = 12;
    if (p.cpu_cores > 0 && p.cpu_cores < 12) {
        int kg = std::max(2, p.cpu_cores);
        if (kg > 16) kg = 16;
        p.suggested_keygen = kg;
    }

    int count = 0;
    cudaError_t st = cudaGetDeviceCount(&count);
    if (st != cudaSuccess) {
        p.error = cudaGetErrorString(st);
        return p;
    }
    p.cuda_devices = count;
    if (count <= 0) {
        p.error = "cudaGetDeviceCount returned 0";
        return p;
    }
    if (p.device_id >= count) p.device_id = 0;

    cudaDeviceProp prop{};
    st = cudaGetDeviceProperties(&prop, p.device_id);
    if (st != cudaSuccess) {
        p.error = cudaGetErrorString(st);
        return p;
    }

    p.cuda_ok = true;
    p.gpu_name = prop.name;
    p.sm_major = prop.major;
    p.sm_minor = prop.minor;
    p.sm_arch = prop.major * 10 + prop.minor;
    p.family = sm_family_name(prop.major, prop.minor);
    p.vram_mib = static_cast<int>(prop.totalGlobalMem / (1024 * 1024));
    p.suggested_lanes = suggested_max_lanes(p.vram_mib);
    p.suggested_batch_m100 = estimate_batch_m100(p.vram_mib, p.suggested_lanes);
    return p;
}

}  // namespace xn
