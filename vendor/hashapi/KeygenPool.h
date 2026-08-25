#pragma once

#include <cstddef>
#include <string>

namespace hashapi {

/// Persistent keygen workers. 0 = half of online CPUs (max 16), then CUDA host cap.
/// Pack-wide: all 8 lanes share this pool (no 8×N spawn storm).
void configureKeygenPool(int threads);
int keygenPoolThreads();
/// Fill `count` keys of `key_length` hex chars into a flat arena (count * key_length).
void keygenFillFlat(char* arena, std::size_t count, std::size_t key_length,
                    const std::string& prefix);

}  // namespace hashapi
