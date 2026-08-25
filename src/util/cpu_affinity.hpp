#pragma once

#include <string>

namespace xn {
namespace cpu {

enum class Role { Bag, Flush, Dashboard, CudaHost };

// Split CPUs from this machine's online count. 0 for bag/flush/dashboard = auto.
// desktop_cores=0 means dedicated miner (Vast): no cores reserved for a desktop session.
void init_layout(int desktop_cores, int bag_cores, int flush_cores, int dashboard_cores);

void pin_this_thread(Role role);

int online_count();
int flush_cpu_count();
int cuda_host_count();
// Keygen workers: min(16, CUDA host CPUs), at least 2. Leaves flush/bag cores free.
int suggested_keygen_threads();
// IO-bound /verify cap from flush CPUs (32..2048). Small boxes stay well below 2048.
int flush_http_cap();

std::string layout_summary();

}  // namespace cpu
}  // namespace xn
