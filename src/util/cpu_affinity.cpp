#include "util/cpu_affinity.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fstream>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cctype>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace xn {
namespace cpu {
namespace {

std::mutex mu;
bool inited = false;
int logical_n = 0;
int physical_cores = 0;
int desktop_n = 0;
int bag_n = 0;
int flush_n = 0;
int dash_n = 0;
int cuda_n = 0;
int keygen_n = 0;
std::vector<int> cpus_bag;
std::vector<int> cpus_flush;
std::vector<int> cpus_dash;
std::vector<int> cpus_cuda;
std::vector<int> cpus_keygen;

int detect_online() {
#ifdef _WIN32
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    int n = static_cast<int>(si.dwNumberOfProcessors);
    if (n < 1) n = 1;
    return n;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1) n = 1;
    return static_cast<int>(n);
#endif
}

#ifndef _WIN32
void parse_cpu_list(const std::string& s, std::vector<int>& out) {
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && !(std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-')) ++i;
        if (i >= s.size()) break;
        int a = 0;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            a = a * 10 + (s[i] - '0');
            ++i;
        }
        int b = a;
        if (i < s.size() && s[i] == '-') {
            ++i;
            b = 0;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
                b = b * 10 + (s[i] - '0');
                ++i;
            }
        }
        if (b < a) std::swap(a, b);
        for (int c = a; c <= b && c < CPU_SETSIZE; ++c) out.push_back(c);
        if (i < s.size() && s[i] == ',') ++i;
    }
}

std::vector<std::vector<int>> detect_physical_cores() {
    std::vector<std::vector<int>> cores;
    std::vector<char> seen(CPU_SETSIZE, 0);
    const int n = detect_online();
    for (int i = 0; i < n && i < CPU_SETSIZE; ++i) {
        if (seen[static_cast<size_t>(i)]) continue;
        std::ostringstream path;
        path << "/sys/devices/system/cpu/cpu" << i << "/topology/thread_siblings_list";
        std::ifstream in(path.str());
        std::string list;
        std::vector<int> lps;
        if (in && std::getline(in, list)) {
            parse_cpu_list(list, lps);
        }
        if (lps.empty()) lps.push_back(i);
        std::sort(lps.begin(), lps.end());
        lps.erase(std::unique(lps.begin(), lps.end()), lps.end());
        for (int c : lps) {
            if (c >= 0 && c < CPU_SETSIZE) seen[static_cast<size_t>(c)] = 1;
        }
        cores.push_back(std::move(lps));
    }
    if (cores.empty()) {
        for (int i = 0; i < n; ++i) cores.push_back({i});
    }
    return cores;
}

void append_core(std::vector<int>& dest, const std::vector<int>& lps) {
    dest.insert(dest.end(), lps.begin(), lps.end());
}
#endif

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

int physical_core_count() {
    std::lock_guard<std::mutex> lock(mu);
    return physical_cores > 0 ? physical_cores : detect_online();
}

void init_layout(int desktop_cores, int bag_cores, int flush_cores, int dashboard_cores) {
    std::lock_guard<std::mutex> lock(mu);
    logical_n = detect_online();

#ifdef _WIN32
    const int n = logical_n;
    physical_cores = n;
    if (desktop_cores < 0) desktop_cores = 0;
    if (desktop_cores > n / 2) desktop_cores = n / 2;
    int remain = n - desktop_cores;
    if (remain < 1) {
        desktop_cores = 0;
        remain = n;
    }
    if (flush_cores <= 0) {
        if (remain <= 6) flush_cores = 1;
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
    int idx = desktop_cores;
    take(cpus_bag, idx, n, bag_cores);
    take(cpus_flush, idx, n, flush_cores);
    take(cpus_dash, idx, n, dashboard_cores);
    take(cpus_cuda, idx, n, n - idx);
    if (cpus_cuda.empty()) cpus_cuda = cpus_flush;
    cpus_keygen.clear();
    for (int i = 0; i < n && static_cast<int>(cpus_keygen.size()) < 12; ++i) {
        bool cuda = std::find(cpus_cuda.begin(), cpus_cuda.end(), i) != cpus_cuda.end();
        if (!cuda) cpus_keygen.push_back(i);
    }
    if (cpus_keygen.empty()) cpus_keygen = cpus_cuda;
    desktop_n = desktop_cores;
    bag_n = bag_cores;
    flush_n = flush_cores;
    dash_n = dashboard_cores;
    cuda_n = static_cast<int>(cpus_cuda.size());
    keygen_n = static_cast<int>(cpus_keygen.size());
    inited = true;
#else
    auto cores = detect_physical_cores();
    physical_cores = static_cast<int>(cores.size());
    if (physical_cores < 1) physical_cores = 1;

    // Desktop miner slice on an 8-core Vast box (7800X3D): last 2 physical
    // cores = CUDA spin-wait, first 6 = keygen (12 SMT threads). bag/flush/
    // dashboard share the keygen cores (they block on IO, they do not spin).
    int nphys = physical_cores;
    int cuda_phys = (nphys >= 4) ? 2 : 1;
    if (cuda_phys >= nphys) cuda_phys = nphys - 1;
    if (cuda_phys < 1) cuda_phys = 1;
    int keygen_phys = std::min(6, std::max(1, nphys - cuda_phys));

    if (desktop_cores < 0) desktop_cores = 0;
    if (desktop_cores > nphys / 2) desktop_cores = nphys / 2;

    if (nphys >= 8) {
        if (bag_cores <= 0) bag_cores = 0;
        if (flush_cores <= 0) flush_cores = 6;
        if (dashboard_cores <= 0) dashboard_cores = 0;
    } else {
        if (flush_cores <= 0) flush_cores = 1;
        if (bag_cores <= 0) bag_cores = (nphys >= 6) ? 1 : 0;
        if (dashboard_cores <= 0) dashboard_cores = (nphys >= 6) ? 1 : 0;
    }

    int non_cuda = nphys - cuda_phys;
    while (bag_cores + flush_cores + dashboard_cores > non_cuda && non_cuda > 0) {
        if (dashboard_cores > 0) --dashboard_cores;
        else if (bag_cores > 0) --bag_cores;
        else if (flush_cores > 1) --flush_cores;
        else
            break;
    }

    cpus_bag.clear();
    cpus_flush.clear();
    cpus_dash.clear();
    cpus_cuda.clear();
    cpus_keygen.clear();

    // CUDA host = last physical cores (desktop: last 2 of the miner slice).
    for (int p = nphys - cuda_phys; p < nphys; ++p) append_core(cpus_cuda, cores[static_cast<size_t>(p)]);
    // Keygen = first keygen_phys cores (desktop: first 6 of CCD0).
    for (int p = 0; p < keygen_phys; ++p) append_core(cpus_keygen, cores[static_cast<size_t>(p)]);

    int idx = 0;
    auto take_phys = [&](std::vector<int>& dest, int count) {
        dest.clear();
        for (int k = 0; k < count && idx < nphys - cuda_phys; ++k, ++idx) {
            append_core(dest, cores[static_cast<size_t>(idx)]);
        }
    };
    take_phys(cpus_bag, bag_cores);
    take_phys(cpus_flush, flush_cores);
    take_phys(cpus_dash, dashboard_cores);
    if (cpus_bag.empty()) cpus_bag = cpus_keygen;
    if (cpus_flush.empty()) cpus_flush = cpus_keygen;
    if (cpus_dash.empty()) cpus_dash = cpus_keygen;
    if (cpus_cuda.empty()) cpus_cuda = cpus_flush;
    if (cpus_keygen.empty()) cpus_keygen = cpus_bag;

    desktop_n = desktop_cores;
    bag_n = bag_cores;
    flush_n = flush_cores;
    dash_n = dashboard_cores;
    cuda_n = cuda_phys;
    keygen_n = keygen_phys;
    inited = true;
#endif
}

void pin_this_thread(Role role) {
    std::vector<int> cpus;
    {
        std::lock_guard<std::mutex> lock(mu);
        if (!inited) return;
        switch (role) {
            case Role::Bag:
                cpus = cpus_bag.empty() ? cpus_keygen : cpus_bag;
                break;
            case Role::Flush:
                cpus = cpus_flush.empty() ? cpus_keygen : cpus_flush;
                break;
            case Role::Dashboard:
                cpus = cpus_dash.empty() ? cpus_keygen : cpus_dash;
                break;
            case Role::CudaHost:
                cpus = cpus_cuda;
                break;
            case Role::Keygen:
                cpus = cpus_keygen.empty() ? cpus_bag : cpus_keygen;
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
    return cuda_n > 0 ? cuda_n : 2;
}

int suggested_keygen_threads() {
    std::lock_guard<std::mutex> lock(mu);
    // Desktop default: 12 threads on 6 physical cores (SMT). An 8-core Vast
    // box has those 6 cores once CUDA host takes the last 2.
    int phys = keygen_n > 0 ? keygen_n : 6;
    int n = phys * 2;
    if (n < 2) n = 2;
    if (n > 16) n = 16;
    if (n < 12 && phys >= 6) n = 12;
    if (phys >= 6) n = 12;
    return n;
}

std::vector<int> keygen_cpu_list() {
    std::lock_guard<std::mutex> lock(mu);
    return cpus_keygen;
}

int flush_http_cap() {
    const int fc = flush_cpu_count();
    int cap = 256;
    if (fc <= 1) cap = 256;
    else if (fc <= 4) cap = 1024;
    else cap = 2048;
    if (cap > 2048) cap = 2048;
    return cap;
}

std::string layout_summary() {
    std::lock_guard<std::mutex> lock(mu);
    std::ostringstream oss;
    int cap = 256;
    if (flush_n <= 1) cap = 256;
    else if (flush_n <= 4) cap = 1024;
    else cap = 2048;
    int kg = keygen_n * 2;
    if (keygen_n >= 6) kg = 12;
    if (kg < 2) kg = 2;
    if (kg > 16) kg = 16;
    oss << physical_cores << " physical cores (" << logical_n << " LPs) — desktop "
        << desktop_n << ", bag " << bag_n << ", flush " << flush_n << ", dashboard " << dash_n
        << ", CUDA host " << cuda_n << " (last cores, spin), keygen " << kg << " threads on first "
        << keygen_n << " cores (not CUDA host), /verify cap " << cap;
    return oss.str();
}

}  // namespace cpu
}  // namespace xn
