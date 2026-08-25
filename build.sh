#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${ROOT}/build"

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake not found. Run ./install-deps.sh first." >&2
  exit 1
fi

# Desktop 16 MH/s is CUDA 13.3. Vast images ship 12.8 (~7 MH/s on the same kernel).
# Install nvcc 13.x without changing the GPU driver, then compile with it.
# shellcheck disable=SC1091
source "${ROOT}/scripts/ensure-cuda13.sh"

if ! command -v nvcc >/dev/null 2>&1; then
  echo "nvcc not found. Install the NVIDIA CUDA Toolkit and put it on PATH." >&2
  exit 1
fi

NVCC_VER="$(nvcc --version 2>/dev/null | tr '\n' ' ' || true)"
echo "nvcc: ${NVCC_VER}"
if ! nvcc --version 2>/dev/null | grep -q 'release 13'; then
  echo "ERROR: need CUDA 13 nvcc (desktop 16 MH/s compiler). Have: ${NVCC_VER}" >&2
  exit 1
fi

detect_arch() {
  if [[ -n "${CMAKE_CUDA_ARCHITECTURES:-}" ]]; then
    echo "${CMAKE_CUDA_ARCHITECTURES}"
    return
  fi
  local cap=""
  if command -v nvidia-smi >/dev/null 2>&1; then
    cap="$(nvidia-smi --query-gpu=compute_cap --format=csv,noheader 2>/dev/null | head -n1 | tr -d '[:space:]')"
  fi
  # 5090 = 12.0. Use sm_120 like the winning Windows Desktop binary
  # (16 MH/s / 94% util). sm_120a on Vast stayed ~7 MH/s / 50% util.
  if [[ "${cap}" == "12.0" || "${cap}" == "12.1" || -z "${cap}" ]]; then
    echo "120"
    return
  fi
  if [[ "${cap}" =~ ^([0-9]+)\.([0-9]+)$ ]]; then
    echo "${BASH_REMATCH[1]}${BASH_REMATCH[2]}"
    return
  fi
  echo "120"
}

ARCH="$(detect_arch)"
GEN=()
if command -v ninja >/dev/null 2>&1; then
  GEN=(-G Ninja)
fi

CMAKE_ARGS=(
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_CUDA_ARCHITECTURES="${ARCH}"
)
if [[ -n "${CMAKE_CUDA_COMPILER:-}" ]]; then
  CMAKE_ARGS+=(-DCMAKE_CUDA_COMPILER="${CMAKE_CUDA_COMPILER}")
fi
if [[ -n "${CUDAToolkit_ROOT:-}" ]]; then
  CMAKE_ARGS+=(-DCUDAToolkit_ROOT="${CUDAToolkit_ROOT}")
fi

echo "Building xnminer (Blackwell CUDA) CMAKE_CUDA_ARCHITECTURES=${ARCH} compiler=${CMAKE_CUDA_COMPILER:-nvcc}"
rm -f "${BUILD}/CMakeCache.txt"
cmake -S "${ROOT}" -B "${BUILD}" "${GEN[@]}" "${CMAKE_ARGS[@]}"
cmake --build "${BUILD}" --parallel

echo
if command -v cuobjdump >/dev/null 2>&1 && [[ -x "${BUILD}/bin/xnminer" ]]; then
  echo "Embedded GPU ELF (must list sm_120 / sm_120a):"
  cuobjdump -lelf "${BUILD}/bin/xnminer" || true
fi
echo "OK: ${BUILD}/bin/xnminer"
mkdir -p "${ROOT}/data"
nvcc --version > "${ROOT}/data/nvcc_version" 2>/dev/null || true
if command -v git >/dev/null 2>&1 && git -C "${ROOT}" rev-parse HEAD >/dev/null 2>&1; then
  git -C "${ROOT}" rev-parse HEAD > "${ROOT}/data/build_sha"
fi
