#include "mining/gpu_test.hpp"

#include "efficiency/thermal_policy.hpp"
#include "efficiency/vram_policy.hpp"
#include "mining/cuda_engine.hpp"
#include "mining/vram_batch.hpp"
#include "monitoring/nvml_monitor.hpp"
#include "util/cpu_affinity.hpp"
#include "util/hardware.hpp"

#include <cuda_runtime.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace xn {
namespace {

struct TrialSpec {
    int lanes = 1;
    int batch_per_lane = 0;  // 0 = VRAM auto
    const char* tag = "";
};

struct TrialResult {
    TrialSpec spec;
    int planned_lanes = 0;
    int planned_batch = 0;
    int total_jobs = 0;
    int64_t hashes = 0;
    double seconds = 0;
    double wall_hs = 0;
    double engine_hs = 0;
    int used_mib = 0;
    int peak_used_mib = 0;
    int gpu_temp = 0;
    int mem_temp = 0;
    int util_pct = 0;
    double power_w = -1;
    int budget_mib = 0;
    int batch_vram_mib = 0;
    bool ok = false;
    std::string error;
};

int align_jobs(int n) {
    if (n <= 0) return 0;
    if (n < 4) return n;
    return (n / 4) * 4;
}

void reset_device(int device_id) {
    cudaSetDevice(device_id);
    cudaDeviceSynchronize();
    cudaDeviceReset();
}

void sample_nvml(NvmlMonitor& nvml, TrialResult& r) {
    auto snap = nvml.snapshot();
    if (!snap) return;
    r.used_mib = snap->used_mib;
    r.peak_used_mib = std::max(r.peak_used_mib, snap->used_mib);
    r.gpu_temp = std::max(r.gpu_temp, snap->temperature_c);
    r.mem_temp = std::max(r.mem_temp, snap->memory_junction_c);
    r.util_pct = std::max(r.util_pct, snap->util_pct);
    if (snap->power_w > 0) r.power_w = std::max(r.power_w, snap->power_w);
}

TrialResult run_trial(const Settings& base, const TrialSpec& spec, int seconds, NvmlMonitor& nvml) {
    TrialResult r;
    r.spec = spec;
    Settings s = base;
    s.cuda_max_lanes = spec.lanes;
    s.cuda_batch_size = spec.batch_per_lane;
    s.gpu_thermal_batch_enabled = false;
    s.gpu_thermal_start_scale = 1.0;
    s.xuni_mining_enabled = false;
    s.memory_cost = s.force_mine_memory_cost > 0 ? s.force_mine_memory_cost : s.memory_cost;
    if (s.memory_cost <= 0) s.memory_cost = kHybridForceMineMemoryCost;

    std::cout << "  trial  lanes=" << spec.lanes << " batch="
              << (spec.batch_per_lane > 0 ? std::to_string(spec.batch_per_lane) : std::string("auto"))
              << (spec.tag && spec.tag[0] ? (std::string("  (") + spec.tag + ")") : std::string())
              << " ..." << std::flush;

    try {
        CudaEngine engine(s);
        engine.start();
        engine.set_difficulty(s.memory_cost);
        engine.set_thermal_batch_scale(1.0);

        r.planned_lanes = engine.active_lanes();
        r.planned_batch = engine.batch_size();
        r.total_jobs = r.planned_lanes * r.planned_batch;
        if (const auto* plan = engine.vram_plan()) {
            r.budget_mib = plan->budget_mib;
            r.batch_vram_mib = plan->batch_vram_mib;
        }
        if (r.planned_batch <= 0 || r.planned_lanes <= 0) {
            throw std::runtime_error("planner returned empty pack");
        }

        const int warmup = 2;
        for (int i = 0; i < warmup; ++i) {
            engine.mine_batch();
            sample_nvml(nvml, r);
        }

        const auto t0 = std::chrono::steady_clock::now();
        double engine_hs_sum = 0;
        int engine_hs_n = 0;
        int waves = 0;
        const auto deadline = t0 + std::chrono::seconds(std::max(8, seconds));
        while (std::chrono::steady_clock::now() < deadline || waves < 3) {
            auto batch = engine.mine_batch();
            r.hashes += batch.hashes_done;
            if (engine.last_hashrate() > 0) {
                engine_hs_sum += engine.last_hashrate();
                ++engine_hs_n;
            }
            sample_nvml(nvml, r);
            ++waves;
            if (waves > 200) break;
        }
        engine.drain_pipeline();
        const auto t1 = std::chrono::steady_clock::now();
        r.seconds = std::chrono::duration<double>(t1 - t0).count();
        if (r.seconds > 0.001) r.wall_hs = static_cast<double>(r.hashes) / r.seconds;
        if (engine_hs_n > 0) r.engine_hs = engine_hs_sum / engine_hs_n;
        sample_nvml(nvml, r);

        size_t free_b = 0, total_b = 0;
        if (cudaMemGetInfo(&free_b, &total_b) == cudaSuccess) {
            r.used_mib = static_cast<int>((total_b - free_b) / (1024 * 1024));
            r.peak_used_mib = std::max(r.peak_used_mib, r.used_mib);
        }

        engine.stop();
        r.ok = r.hashes > 0 && r.wall_hs > 0;
        if (!r.ok) r.error = "no hashes measured";
    } catch (const std::exception& ex) {
        r.error = ex.what();
        r.ok = false;
    } catch (...) {
        r.error = "unknown CUDA failure";
        r.ok = false;
    }

    try {
        reset_device(base.device_id);
    } catch (...) {
    }

    if (r.ok) {
        std::cout << "  " << std::fixed << std::setprecision(1) << r.wall_hs << " H/s  "
                  << r.planned_lanes << "x" << r.planned_batch << "  vram=" << r.peak_used_mib
                  << "MiB  gpu=" << r.gpu_temp << "C mem=" << r.mem_temp << "C\n";
    } else {
        std::cout << "  FAIL  " << r.error << "\n";
    }
    std::cout << std::flush;
    return r;
}

void print_plan_table(uint64_t total_bytes, uint64_t free_bytes, const VramCaps& caps, int m) {
    std::cout << "VRAM plan at m=" << m << "  target=" << caps.target_mib
              << "MiB  headroom=" << caps.headroom_mib << "MiB  overhead="
              << caps.runtime_overhead_mib << "MiB\n";
    std::cout << "  lanes  batch/lane  total jobs  pack MiB  used MiB  headroom\n";
    for (int lanes : {1, 2, 4, 8, 16, 32}) {
        auto plan = plan_cuda_batch(total_bytes, free_bytes, caps.target_mib, caps.headroom_mib, m, m,
                                    lanes, 1, 0, 0, caps.runtime_overhead_mib);
        std::cout << "  " << std::setw(5) << plan.lanes << "  " << std::setw(10) << plan.batch_per_lane
                  << "  " << std::setw(10) << (plan.lanes * plan.batch_per_lane) << "  "
                  << std::setw(8) << plan.batch_vram_mib << "  " << std::setw(8)
                  << plan.projected_used_mib << "  " << plan.projected_headroom_mib << "\n";
    }
}

bool better(const TrialResult& a, const TrialResult& b) {
    if (a.ok != b.ok) return a.ok;
    if (!a.ok) return false;
    const double hi = std::max(a.wall_hs, b.wall_hs);
    if (hi <= 0) return false;
    const double rel = std::abs(a.wall_hs - b.wall_hs) / hi;
    if (rel > 0.03) return a.wall_hs > b.wall_hs;
    // Within 3%: cooler memory junction, then fewer lanes (less host overhead).
    if (a.mem_temp != b.mem_temp && a.mem_temp > 0 && b.mem_temp > 0) return a.mem_temp < b.mem_temp;
    if (a.planned_lanes != b.planned_lanes) return a.planned_lanes < b.planned_lanes;
    return a.wall_hs > b.wall_hs;
}

}  // namespace

int run_gpu_hardware_test(Settings settings, int seconds_per_trial) {
#ifdef _WIN32
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
#endif
    if (seconds_per_trial <= 0) seconds_per_trial = 15;
    if (seconds_per_trial < 8) seconds_per_trial = 8;
    if (seconds_per_trial > 60) seconds_per_trial = 60;

    if (settings.address.empty()) {
        settings.address = "0x1111111111111111111111111111111111111111";
    }
    settings.gpu_thermal_batch_enabled = false;
    settings.gpu_thermal_start_scale = 1.0;
    settings.xuni_mining_enabled = false;
    settings.gpu_power_boost_enabled = false;
    if (settings.force_mine_memory_cost <= 0) {
        settings.force_mine_memory_cost = kHybridForceMineMemoryCost;
    }
    settings.memory_cost = settings.force_mine_memory_cost;
    const int m = settings.memory_cost;

    cpu::init_layout(settings.desktop_cpu_cores, settings.bag_sort_cpu_cores, settings.flush_cpu_cores,
                     settings.dashboard_cpu_cores);

    cudaSetDeviceFlags(cudaDeviceScheduleSpin | cudaDeviceMapHost);
    cudaError_t sd = cudaSetDevice(settings.device_id);
    if (sd != cudaSuccess) {
        std::cerr << "cudaSetDevice failed: " << cudaGetErrorString(sd) << "\n";
        return 1;
    }

    const auto hw = probe_hardware(settings.device_id);
    if (!hw.cuda_ok) {
        std::cerr << "No CUDA GPU: " << hw.error << "\n";
        return 1;
    }

    size_t free_b = 0, total_b = 0;
    cudaMemGetInfo(&free_b, &total_b);
    const int total_mib = static_cast<int>(total_b / (1024 * 1024));
    auto caps = resolve_vram_caps(
        total_mib, settings.target_vram_pct, settings.desktop_headroom_pct,
        settings.emergency_vram_pct, settings.min_headroom_pct, settings.runtime_overhead_pct,
        settings.min_headroom_floor_mib, settings.runtime_overhead_floor_mib, settings.target_vram_mib,
        settings.headroom_mib, settings.emergency_vram_mib, settings.min_headroom_mib,
        settings.cuda_runtime_overhead_mib);

    std::cout << "\n=== GPU hardware test ===\n"
              << hw.summary() << "\n"
              << "Force-mine m=" << m << "  " << std::fixed << std::setprecision(2)
              << (bytes_per_attempt(m) / (1024.0 * 1024.0)) << " MiB/hash  "
              << seconds_per_trial << "s/trial  thermal off  pack=" << settings.target_vram_pct
              << "%\n"
              << cpu::layout_summary() << "\n\n";
    print_plan_table(total_b, free_b, caps, m);

    NvmlMonitor nvml(settings.device_id);
    if (nvml.available()) {
        auto snap = nvml.snapshot();
        if (snap) {
            std::cout << "NVML  used=" << snap->used_mib << "MiB  gpu=" << snap->temperature_c
                      << "C  mem=" << snap->memory_junction_c << "C  power="
                      << std::setprecision(0) << snap->power_w << "W\n";
        }
    } else {
        std::cout << "NVML unavailable — temps/util will be blank\n";
    }

    const int vram_lanes = suggested_max_lanes(total_mib);
    std::vector<TrialSpec> phase_a;
    for (int lanes : {1, 2, 4, 8, 16, 32}) {
        if (lanes > vram_lanes) continue;
        TrialSpec spec;
        spec.lanes = lanes;
        spec.batch_per_lane = 0;
        spec.tag = "auto pack";
        phase_a.push_back(spec);
    }

    std::cout << "\n-- phase A: lane split (auto batch fills " << settings.target_vram_pct
              << "% VRAM) --\n";
    std::vector<TrialResult> results;
    for (const auto& spec : phase_a) {
        results.push_back(run_trial(settings, spec, seconds_per_trial, nvml));
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }

    TrialResult best{};
    for (const auto& r : results) {
        if (!best.ok || better(r, best)) best = r;
    }
    if (!best.ok) {
        std::cerr << "\nAll lane trials failed. Last error: "
                  << (results.empty() ? "none" : results.back().error) << "\n";
        return 1;
    }

    // Second-best lane, if close, also gets a batch sweep.
    TrialResult second = best;
    for (const auto& r : results) {
        if (!r.ok) continue;
        if (r.planned_lanes == best.planned_lanes) continue;
        if (second.planned_lanes == best.planned_lanes || better(r, second)) second = r;
    }

    auto enqueue_batch_sweep = [&](int lanes, int auto_batch) {
        if (auto_batch <= 0) return;
        const double scales[] = {0.55, 0.70, 0.85, 1.00};
        for (double sc : scales) {
            int b = align_jobs(apply_batch_scale(auto_batch, sc));
            if (b < 1) continue;
            if (b == auto_batch && std::abs(sc - 1.0) < 0.001) continue;  // already in phase A
            bool dup = false;
            for (const auto& prev : results) {
                if (prev.ok && prev.planned_lanes == lanes && prev.planned_batch == b) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;
            TrialSpec spec;
            spec.lanes = lanes;
            spec.batch_per_lane = b;
            spec.tag = (std::abs(sc - 0.70) < 0.001) ? "thermal floor 70%" : "batch scale";
            results.push_back(run_trial(settings, spec, seconds_per_trial, nvml));
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
        }
    };

    std::cout << "\n-- phase B: batch size on best lane split (" << best.planned_lanes << "x"
              << best.planned_batch << ") --\n";
    enqueue_batch_sweep(best.planned_lanes, best.planned_batch);
    if (second.ok && second.planned_lanes != best.planned_lanes &&
        second.wall_hs >= best.wall_hs * 0.92) {
        std::cout << "\n-- phase B2: close 2nd lane split (" << second.planned_lanes << "x"
                  << second.planned_batch << ") --\n";
        enqueue_batch_sweep(second.planned_lanes, second.planned_batch);
    }

    for (const auto& r : results) {
        if (!best.ok || better(r, best)) best = r;
    }

    std::cout << "\n=== results (m=" << m << ") ===\n";
    std::cout << std::left << std::setw(8) << "lanes" << std::setw(10) << "batch"
              << std::setw(10) << "jobs" << std::setw(12) << "wall H/s" << std::setw(12)
              << "engine H/s" << std::setw(10) << "VRAM" << std::setw(8) << "gpuC"
              << std::setw(8) << "memC" << std::setw(8) << "util" << "notes\n";
    for (const auto& r : results) {
        std::ostringstream note;
        if (!r.ok) note << r.error;
        else if (r.spec.tag && r.spec.tag[0]) note << r.spec.tag;
        std::cout << std::left << std::setw(8) << (r.ok ? r.planned_lanes : r.spec.lanes)
                  << std::setw(10) << (r.ok ? r.planned_batch : r.spec.batch_per_lane)
                  << std::setw(10) << r.total_jobs << std::setw(12) << std::fixed
                  << std::setprecision(1) << r.wall_hs << std::setw(12) << r.engine_hs
                  << std::setw(10) << r.peak_used_mib << std::setw(8) << r.gpu_temp
                  << std::setw(8) << r.mem_temp << std::setw(8) << r.util_pct << note.str()
                  << "\n";
    }

    std::cout << "\nBEST  " << best.planned_lanes << " lanes x batch " << best.planned_batch
              << "  (" << best.total_jobs << " jobs)  " << std::fixed << std::setprecision(1)
              << best.wall_hs << " H/s  VRAM " << best.peak_used_mib << " MiB  mem "
              << best.mem_temp << "C\n"
              << "Recommended miner.ini:\n"
              << "  [cuda]\n"
              << "  max_lanes = " << best.planned_lanes << "\n"
              << "  batch_size = " << best.planned_batch << "\n"
              << "  (leave max_batch_size = 0)\n";
    return 0;
}

}  // namespace xn
