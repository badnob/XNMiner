#pragma once

#include "config/settings.hpp"

namespace xn {

// Live CUDA sweep of lane count and batch size at the hybrid force-mine m=.
// Prints a table and the recommended [cuda] max_lanes / batch_size.
int run_gpu_hardware_test(Settings settings, int seconds_per_trial);

}  // namespace xn
