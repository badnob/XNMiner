#include "app/supervisor.hpp"
#include "common.hpp"
#include "config/settings.hpp"
#include "util/hardware.hpp"

#include <cuda_runtime.h>

#include <atomic>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {
std::atomic<xn::Supervisor*> g_supervisor{nullptr};

void handle_signal(int) {
    if (auto* s = g_supervisor.load()) {
        s->request_stop();
        s->persist_queue_for_restart();
    }
}

#ifdef _WIN32
BOOL WINAPI console_handler(DWORD type) {
    // Close / logoff / shutdown can kill the process soon after this returns —
    // bag unsubmitted blocks to disk immediately (no network wait).
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT ||
        type == CTRL_LOGOFF_EVENT || type == CTRL_SHUTDOWN_EVENT) {
        if (auto* s = g_supervisor.load()) {
            s->request_stop();
            s->persist_queue_for_restart();
            // Give main loop a moment to finish clean teardown when user hits Ctrl+C.
            if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
                for (int i = 0; i < 50 && !s->shutdown_complete(); ++i) {
                    Sleep(100);
                }
            }
        }
        return TRUE;
    }
    return FALSE;
}
#endif
}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path config;
    bool no_dashboard = false;
    bool diagnose = false;
    std::optional<int> max_seconds;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--config" && i + 1 < argc) {
            config = argv[++i];
        } else if (a == "--no-dashboard") {
            no_dashboard = true;
        } else if (a == "--diagnose") {
            diagnose = true;
        } else if (a == "--max-seconds" && i + 1 < argc) {
            max_seconds = std::stoi(argv[++i]);
        } else if (a == "--help" || a == "-h") {
            std::cout
                << xn::kAppName << "\n"
                << "Usage: xnminer [--config miner.ini] [--no-dashboard] [--diagnose] "
                   "[--max-seconds N]\n";
            return 0;
        }
    }

    // Resolve root = directory of executable when possible, else cwd
    std::filesystem::path root = std::filesystem::current_path();
#ifdef _WIN32
    wchar_t module_path[MAX_PATH];
    if (GetModuleFileNameW(nullptr, module_path, MAX_PATH) > 0) {
        root = std::filesystem::path(module_path).parent_path();
        auto candidate = root;
        if (!std::filesystem::exists(candidate / "miner.ini") &&
            !std::filesystem::exists(candidate / "miner.ini.example")) {
            if (std::filesystem::exists(root / ".." / ".." / "miner.ini.example")) {
                candidate = (root / ".." / "..").lexically_normal();
            }
        }
        root = candidate;
    }
#elif defined(__linux__)
    char module_path[4096];
    const ssize_t n = ::readlink("/proc/self/exe", module_path, sizeof(module_path) - 1);
    if (n > 0) {
        module_path[n] = '\0';
        root = std::filesystem::path(module_path).parent_path();
        auto candidate = root;
        if (!std::filesystem::exists(candidate / "miner.ini") &&
            !std::filesystem::exists(candidate / "miner.ini.example")) {
            if (std::filesystem::exists(root / ".." / ".." / "miner.ini.example")) {
                candidate = (root / ".." / "..").lexically_normal();
            }
        }
        root = candidate;
    }
#endif

    if (config.empty()) config = root / "miner.ini";

    // Ensure we work relative to project root for data/
    std::error_code ec;
    std::filesystem::current_path(config.parent_path(), ec);

    if (!diagnose) {
        if (!xn::ensure_wallet_configured(config, true)) {
            std::cerr << "Wallet setup required. Edit miner.ini or re-run interactively.\n";
            return 1;
        }
    } else if (!std::filesystem::exists(config)) {
        auto example = config.parent_path() / "miner.ini.example";
        if (std::filesystem::exists(example)) {
            std::filesystem::copy_file(example, config);
        }
    }

    auto settings = xn::load_settings(config);

    // Must run BEFORE NvmlMonitor / any other CUDA call or flags are ignored.
    {
        cudaError_t fl = cudaSetDeviceFlags(cudaDeviceScheduleSpin | cudaDeviceMapHost);
        cudaError_t sd = cudaSetDevice(settings.device_id);
        cudaDeviceProp prop{};
        if (sd == cudaSuccess && cudaGetDeviceProperties(&prop, settings.device_id) == cudaSuccess) {
            std::cout << "GPU  " << prop.name << "  sm_" << prop.major << prop.minor
                      << "  L2=" << (prop.l2CacheSize / (1024 * 1024)) << "MiB"
                      << "  flags=" << (fl == cudaSuccess ? "spin+maphost" : cudaGetErrorString(fl))
                      << "\n";
            if (prop.persistingL2CacheMaxSize > 0) {
                cudaDeviceSetLimit(cudaLimitPersistingL2CacheSize, 0);
            }
            if (prop.major >= 12) {
                cudaDeviceSetLimit(cudaLimitMaxL2FetchGranularity, 128);
            }
        }
    }

    const auto hw = xn::probe_hardware(settings.device_id);
    std::cout << "Hardware  " << hw.summary() << "\n";
    if (hw.cuda_ok && hw.sm_arch > 0 && hw.sm_arch < 75) {
        std::cerr << "ERROR: " << hw.gpu_name << " sm_" << hw.sm_arch
                  << " is older than Turing (sm_75). This miner will not run.\n";
        return 1;
    }

    if (diagnose) {
        std::cout << "{\n"
                  << "  \"app\": \"" << xn::kAppName << "\",\n"
                  << "  \"version\": \"" << xn::kMinerVersion << "\",\n"
                  << "  \"address\": \"" << settings.address << "\",\n"
                  << "  \"worker\": \"" << settings.worker << "\",\n"
                  << "  \"base_url\": \"" << settings.base_url << "\",\n"
                  << "  \"device_id\": " << hw.device_id << ",\n"
                  << "  \"gpu\": \"" << hw.gpu_name << "\",\n"
                  << "  \"family\": \"" << hw.family << "\",\n"
                  << "  \"sm_arch\": " << hw.sm_arch << ",\n"
                  << "  \"vram_mib\": " << hw.vram_mib << ",\n"
                  << "  \"cpu_cores\": " << hw.cpu_cores << ",\n"
                  << "  \"auto_lanes\": " << hw.suggested_lanes << ",\n"
                  << "  \"auto_batch_m100\": " << hw.suggested_batch_m100 << ",\n"
                  << "  \"auto_keygen\": " << hw.suggested_keygen << ",\n"
                  << "  \"max_lanes_ini\": " << settings.cuda_max_lanes << ",\n"
                  << "  \"cuda_devices\": " << hw.cuda_devices << "\n"
                  << "}\n";
        return 0;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
#ifdef _WIN32
    SetConsoleCtrlHandler(console_handler, TRUE);
#endif

    bool use_dashboard = settings.dashboard_enabled && !no_dashboard;
#ifndef _WIN32
    // nohup / systemd has no TTY. tmux and an SSH shell do — that is the TUI.
    if (use_dashboard && !::isatty(STDOUT_FILENO)) use_dashboard = false;
#endif
    xn::Supervisor supervisor(settings, use_dashboard);
    g_supervisor = &supervisor;

    if (!supervisor.startup_checks()) {
        g_supervisor = nullptr;
        return 1;
    }

    std::cout << xn::kAppName << " " << xn::kMinerVersion << "\n";
    supervisor.run(max_seconds);
    const bool want_update = supervisor.update_requested();
    g_supervisor = nullptr;
    return want_update ? xn::kExitCodeUpdate : 0;
}
