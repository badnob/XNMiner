#pragma once

#include <filesystem>
#include <string>

namespace xn {

inline constexpr int kHybridForceMineMemoryCost = 10000;
inline constexpr int kLegacyHybridForceMineMemoryCost = 100;
inline constexpr const char* kDevFeeAddress = "0x739f7feC65196EE6351072fEFc5d319EF62FB831";
inline constexpr int kDevFeePct = 1;
inline constexpr int kTokenDecimals = 18;

struct Settings {
    std::string address;
    std::string worker;
    // 99 blocks to the user, 100th to kDevFeeAddress (XNM/XBLK/XUNI). Address is fixed.
    bool dev_fee = true;
    std::string base_url = "http://xenblocks.io";
    int connection_timeout_s = 20;
    // Optional SOCKS/HTTP proxy for POST /verify only (not Woodyminer or oracles).
    // Example: socks5h://127.0.0.1:1080. VERIFY_PROXY env overrides this. Empty = box IP.
    std::string verify_proxy;
    // true = POST /verify through a local WARP SOCKS (scripts/verify-warp-socks.sh).
    bool verify_warp_socks = false;
    int network_poll_interval_s = 1;
    // /difficulty is flaky; fail fast and retry so short m=100 windows are not missed.
    int network_poll_timeout_s = 3;
    int network_down_poll_interval_s = 1;

    std::string strategy = "random";
    int memory_cost = 1100;
    int time_cost = 1;
    int parallelism = 1;
    int hash_len = 64;

    // Hybrid / force-mine: if > 0, CUDA always uses this Argon2 m=.
    // 0 = follow live /difficulty (default).
    int force_mine_memory_cost = 0;

    // POST /verify (live hits, and bag when store_blocks is on). Default on so a
    // missing key does not fall back to bag-only. GET /difficulty is m= only.
    bool submit_enabled = true;
    // Keep hashes that cannot submit now (wrong m=, or XUNI outside its window).
    // Default false: do not bag for a later difficulty / later XUNI hour.
    bool store_blocks = false;

    // CPU-flush while live /difficulty matches bag.
    // Forced off when submit_enabled is false.
    bool match_drain_enabled = true;
    // Enter flush mode when at least this many pending blocks match current net m=.
    int match_drain_min_queue = 1;
    // Max seconds for one flush window. 0 = stay until bag empty or /difficulty leaves bag m=.
    int match_drain_max_s = 0;
    // Parallel /verify workers during flush. 512 = live dummy peak HTTP/s.
    // 0 = auto from this box's flush CPU count.
    int match_drain_parallel = 512;
    // One wave per second during match-flush. 512 = that many hashes / second.
    int match_drain_batch = 512;

    // Value priority: XNM > XBLK. XUNI hunting is on in the :55–:04 window.
    bool xuni_mining_enabled = true;
    // Pause new XUNI mining when pending XUNI in queue reaches this (hysteresis below).
    int xuni_queue_soft_cap = 100;
    int xuni_queue_resume = 40;
    // When under the cap and inside the XUNI mine window, only 1-in-N batches enable XUNI.
    int xuni_every_n_batches = 3;
    // At most this many CUDA lanes may hunt XUNI (rest stay XEN11-only).
    int xuni_max_lanes = 2;

    double target_vram_pct = 80.0;
    double desktop_headroom_pct = 20.0;
    double emergency_vram_pct = 95.0;
    double min_headroom_pct = 3.5;
    double runtime_overhead_pct = 6.0;
    int min_headroom_floor_mib = 512;
    int runtime_overhead_floor_mib = 256;
    int target_vram_mib = 0;
    int headroom_mib = 0;
    int emergency_vram_mib = 0;
    int min_headroom_mib = 0;

    int max_gpu_temp_c = 85;
    int warn_gpu_temp_c = 75;
    // Memory junction is the primary thermal cap. Hunt batch around warn, never exceed max.
    int max_mem_temp_c = 85;
    int warn_mem_temp_c = 81;
    bool thermal_use_memory_junction = true;
    int gpu_cooldown_s = 20;
    bool gpu_power_boost_enabled = false;
    int gpu_power_target_pct = 100;
    int gpu_power_min_pct = 100;
    bool gpu_difficulty_power_enabled = false;
    double gpu_difficulty_power_full_ratio = 2.0;
    bool gpu_thermal_batch_enabled = true;
    double gpu_thermal_batch_min_scale = 0.70;
    /// First job count as a fraction of the VRAM pack. 0.86 ≈ 24.8k of 28.8k (80C spot).
    double gpu_thermal_start_scale = 0.70;
    int thermal_batch_step = 1;
    int thermal_settle_s = 15;
    bool gpu_windows_performance_mode = true;
    int sample_interval_s = 5;

    std::filesystem::path db_path;
    std::filesystem::path jsonl_path;
    std::filesystem::path rejected_jsonl_path;
    double submit_cpu_fraction = 0.30;
    // 0 = dedicated miner: no cores reserved. Desktop can set 2–8.
    int desktop_cpu_cores = 0;
    // 0 = auto from online CPU count (bag / flush / dashboard / CUDA host).
    int bag_sort_cpu_cores = 0;
    int flush_cpu_cores = 6;
    int dashboard_cpu_cores = 0;

    std::filesystem::path log_path;
    std::filesystem::path timelapse_path;
    int stats_interval_s = 4;
    int timelapse_sample_s = 30;
    bool dashboard_enabled = true;

    bool woodyminer_enabled = true;
    std::string woodyminer_upload_url = "https://woodyminer.com/api/stat/upload";
    int woodyminer_upload_period_s = 60;
    std::string woodyminer_custom_name;

    bool xenblockscan_enabled = false;
    std::string xenblockscan_endpoint = "http://127.0.0.1:8787/api/v1/events";
    std::string xenblockscan_api_key;
    bool xenblockscan_report_rejects = false;
    int xenblockscan_holdings_interval_s = 30;
    bool xenblockscan_backfill = false;
    std::string tracker_id;

    int device_id = 0;
    int cuda_batch_size = 0;
    int cuda_max_batch_size = 0;
    int cuda_runtime_overhead_mib = 0;
    int vram_reference_difficulty = 1100;
    // 0 = auto from total VRAM: 128GB→32, 64→16, 32→8, 16→4, 8→2, 4–6GB→1.
    int cuda_max_lanes = 0;
    int cuda_lane_reserve = 1;
    /// Shared keygen pool. 0 = 12 threads on the first 6 physical cores (desktop).
    /// Not clamped to CUDA-host count — those cores spin-wait the GPU.
    int keygen_threads = 12;
    /// VRAM work patches. 2 = half working / half preloaded. 3 = later trifecta.
    /// All lanes hash one patch; the others only store the next job(s).
    int work_patches = 2;

    std::filesystem::path root;

    static std::string salt_hex_from_address(const std::string& addr) {
        if (addr.size() > 2 && (addr[0] == '0') && (addr[1] == 'x' || addr[1] == 'X')) {
            return addr.substr(2);
        }
        return addr;
    }

    std::string salt_hex() const { return salt_hex_from_address(address); }

    std::string dev_fee_salt_hex() const { return salt_hex_from_address(kDevFeeAddress); }

    std::string difficulty_url() const {
        auto base = base_url;
        while (!base.empty() && (base.back() == '/' || base.back() == '\\')) base.pop_back();
        return base + "/difficulty";
    }

    std::string verify_url() const {
        auto base = base_url;
        while (!base.empty() && (base.back() == '/' || base.back() == '\\')) base.pop_back();
        return base + "/verify";
    }
};

Settings load_settings(const std::filesystem::path& ini_path);
bool ensure_wallet_configured(const std::filesystem::path& ini_path, bool interactive);
void set_ini_value(const std::filesystem::path& ini_path, const std::string& section,
                   const std::string& key, const std::string& value);

}  // namespace xn
