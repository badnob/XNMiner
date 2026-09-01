#!/bin/bash
set -e

# Resolve the root directory of the project
PROJECT_ROOT=$(cd "$(dirname "$0")" && pwd)
cd "$PROJECT_ROOT"

echo "Building from: $PROJECT_ROOT"

# Ensure the build directory exists and is clean
mkdir -p build
cd build

# Configure with appropriate flags
# The -D CMAKE_BUILD_TYPE=Release is standard for production
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build the project
cmake --build . --config Release
