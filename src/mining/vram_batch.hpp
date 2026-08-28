#pragma once

#include <cstdint>
#include <string>

namespace xn {

inline constexpr uint64_t CUDA_ENGINE_RESERVE_BYTES = 100ULL * 1024ULL * 1024ULL;
inline constexpr double BYTES_PER_ATTEMPT_FACTOR = 1.001;

struct CudaVramPlan {
    int batch_size = 0;
    int lanes = 1;
    int batch_per_lane = 0;
    int lane_reserve = 0;
    uint64_t budget_bytes = 0;
    int budget_mib = 0;
    uint64_t batch_vram_bytes = 0;
    int batch_vram_mib = 0;
    int used_before_mib = 0;
    int projected_used_mib = 0;
    int projected_headroom_mib = 0;
    int runtime_overhead_mib = 0;
    int target_mib = 0;
    int effective_target_mib = 0;
    int desktop_headroom_mib = 0;
    int difficulty = 0;

    std::string summary() const;
    bool within_limits() const;
    bool fills_budget(int tolerance_mib = 2) const;
};

double bytes_per_attempt(int difficulty);
int cuda_lane_count(int difficulty, int reference_difficulty, int max_lanes);
// Hunt lanes from this card's total VRAM (1..8). 8GB→2, 16GB→4, 24GB→6, 32GB→8.
// Independent of Argon2 m= — high hybrid m= shrinks batch, not lane count.
int suggested_max_lanes(int total_vram_mib);
uint64_t estimate_batch_vram_bytes(int batch_size, int difficulty);
int memory_limited_batch_size(uint64_t free_vram_bytes, int difficulty,
                              uint64_t reserve_bytes = CUDA_ENGINE_RESERVE_BYTES);
int select_batch_size(uint64_t budget_bytes, int difficulty, int explicit_max_batch = 0,
                      bool fill_vram_cap = true);

CudaVramPlan plan_cuda_batch(uint64_t total_bytes, uint64_t free_bytes, int target_mib,
                             int desktop_headroom_mib, int difficulty, int reference_difficulty,
                             int max_lanes = 4, int lane_reserve = 1, int explicit_batch = 0,
                             int explicit_max_batch = 0, int runtime_overhead_mib = 2048);

}  // namespace xn
