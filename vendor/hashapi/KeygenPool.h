#pragma once

#include <cstddef>
#include <string>

namespace hashapi {

/// Persistent keygen workers. 0 = 12 threads (6 physical cores × SMT), pinned
/// off the CUDA-host spin cores so 8 lanes do not starve the GPU.
void configureKeygenPool(int threads);
int keygenPoolThreads();
/// Fill `count` keys of `key_length` hex chars into a flat arena (count * key_length).
void keygenFillFlat(char* arena, std::size_t count, std::size_t key_length,
                    const std::string& prefix);

}  // namespace hashapi
