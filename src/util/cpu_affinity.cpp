#include "util/cpu_affinity.hpp"

#ifndef _WIN32
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <mutex>
#include <sstream>
#include <vector>

namespace xn {
namespace cpu {
namespace {

std::mutex mu;
bool inited = false;
int physical_cores = 0;
int desktop_n = 0;
int bag_n = 0;
int flush_n = 0;
int dash_n = 0;
int cuda_n = 0;
std::vector<int> cpus_bag;
std::vector<int> cpus_flush;
std::vector<int> cpus_dash;
std::vector<int> cpus_cuda;

int detect_online() {
#ifdef _WIN32
    return 1;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1) n = 1;
    return static_cast<int>(n);
#endif
}

void take(std::vector<int>& dest, int& idx, int ncpu, int count) {
    dest.clear();
    for (int i = 0; i < count && idx < ncpu; ++i) dest.push_back(idx++);
}

void pin_list(const std::vector<int>& cpus) {
#ifndef _WIN32
    if (cpus.empty()) return;
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int c : cpus) {
        if (c >= 0 && c < CPU_SETSIZE) CPU_SET(c, &set);
    }
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#else
    (void)cpus;
#endif
}

}  // namespace

int online_count() { return detect_online(); }

void init_layout(int desktop_cores, int bag_cores, int flush_cores, int dashboard_cores) {
    std::lock_guard<std::mutex> lock(mu);
    const int n = detect_online();
    physical_cores = n;

    if (desktop_cores < 0) desktop_cores = 0;
    if (desktop_cores > n / 2) desktop_cores = n / 2;

    int remain = n - desktop_cores;
    if (remain < 1) {
        desktop_cores = 0;
        remain = n;
    }

    // 0 = pick from core count so a 4-core Vast box and a 16-core box both work.
    if (flush_cores <= 0) {
        if (remain <= 2) flush_cores = 1;
        else if (remain <= 6) flush_cores = 1;
        else if (remain <= 8) flush_cores = 2;
        else if (remain <= 16) flush_cores = std::max(2, remain / 4);
        else flush_cores = 4;
    }
    if (bag_cores <= 0) {
        if (remain <= 4) bag_cores = 0;
        else if (remain <= 8) bag_cores = 1;
        else bag_cores = 2;
    }
    if (dashboard_cores <= 0) {
        if (remain <= 4) dashboard_cores = 0;
        else if (remain <= 8) dashboard_cores = 1;
        else dashboard_cores = 2;
    }

    while (bag_cores + flush_cores + dashboard_cores >= remain && remain > 1) {
        if (dashboard_cores > 0) --dashboard_cores;
        else if (bag_cores > 0) --bag_cores;
        else if (flush_cores > 1) --flush_cores;
        else
            break;
    }
    int cuda_cores = remain - bag_cores - flush_cores - dashboard_cores;
    if (cuda_cores < 1) {
        cuda_cores = 1;
        if (dashboard_cores > 0) --dashboard_cores;
        else if (bag_cores > 0) --bag_cores;
    }

    int idx = desktop_cores;  // skip reserved desktop CPUs (0 .. desktop-1)
    take(cpus_bag, idx, n, bag_cores);
    take(cpus_flush, idx, n, flush_cores);
    take(cpus_dash, idx, n, dashboard_cores);
    take(cpus_cuda, idx, n, n - idx);
    if (cpus_cuda.empty()) cpus_cuda = cpus_flush;

    desktop_n = desktop_cores;
    bag_n = bag_cores;
    flush_n = flush_cores;
    dash_n = dashboard_cores;
    cuda_n = static_cast<int>(cpus_cuda.size());
    inited = true;
}

void pin_this_thread(Role role) {
    std::vector<int> cpus;
    {
        std::lock_guard<std::mutex> lock(mu);
        if (!inited) return;
        switch (role) {
            case Role::Bag:
                cpus = cpus_bag.empty() ? cpus_cuda : cpus_bag;
                break;
            case Role::Flush:
                cpus = cpus_flush.empty() ? cpus_cuda : cpus_flush;
                break;
            case Role::Dashboard:
                cpus = cpus_dash.empty() ? cpus_cuda : cpus_dash;
                break;
            case Role::CudaHost:
                cpus = cpus_cuda;
                break;
        }
    }
    pin_list(cpus);
}

int flush_cpu_count() {
    std::lock_guard<std::mutex> lock(mu);
    return flush_n > 0 ? flush_n : 1;
}

int cuda_host_count() {
    std::lock_guard<std::mutex> lock(mu);
    return cuda_n > 0 ? cuda_n : std::max(1, physical_cores / 2);
}

int suggested_keygen_threads() {
    int n = cuda_host_count();
    if (n > 16) n = 16;
    if (n < 2) n = 2;
    return n;
}

int flush_http_cap() {
    const int fc = flush_cpu_count();
    int cap = fc * 256;
    if (cap < 32) cap = 32;
    if (fc <= 1) cap = 64;
    else if (fc == 2) cap = 512;
    else if (fc <= 4) cap = 1024;
    else cap = 2048;
    if (cap > 2048) cap = 2048;
    return cap;
}

std::string layout_summary() {
    std::lock_guard<std::mutex> lock(mu);
    std::ostringstream oss;
    int cap = 64;
    if (flush_n <= 1) cap = 64;
    else if (flush_n == 2) cap = 512;
    else if (flush_n <= 4) cap = 1024;
    else cap = 2048;
    oss << physical_cores << " CPUs — desktop " << desktop_n << " free, bag " << bag_n
        << ", flush " << flush_n << ", dashboard " << dash_n << ", CUDA host " << cuda_n
        << ", /verify cap " << cap;
    return oss.str();
}

}  // namespace cpu
}  // namespace xn
