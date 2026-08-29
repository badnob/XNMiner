#pragma once

#include <string>
#include <vector>

namespace xn {
namespace cpu {

enum class Role { Bag, Flush, Dashboard, CudaHost, Keygen };

// Split CPUs from physical cores (not SMT threads).
// Dedicated miner (desktop_cores=0, 8+ physical): bag 2, flush 2, dashboard 2,
// CUDA host = last 2 physical cores. Keygen uses the first 6 — not the CUDA
// host slice.
void init_layout(int desktop_cores, int bag_cores, int flush_cores, int dashboard_cores);

void pin_this_thread(Role role);

int online_count();
int physical_core_count();
int flush_cpu_count();
int cuda_host_count();
// Desktop default: 12 threads (6 physical cores × SMT). Not clamped to CUDA host.
int suggested_keygen_threads();
std::vector<int> keygen_cpu_list();
int flush_http_cap();

std::string layout_summary();

}  // namespace cpu
}  // namespace xn
