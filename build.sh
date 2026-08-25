#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${ROOT}/build"

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake not found. Run ./install-deps.sh first." >&2
  exit 1
fi

# GPU / CPU / VRAM → arch, lanes, compiler choice.
# shellcheck disable=SC1091
source "${ROOT}/scripts/detect-hardware.sh"
xn_hardware_report

if [[ "${XN_UNSUPPORTED}" == "1" ]]; then
  echo "ERROR: ${XN_UNSUPPORTED_REASON}" >&2
  echo "This miner targets Turing (RTX 20 / GTX 16) and newer NVIDIA GPUs." >&2
  exit 1
fi

# Blackwell: CUDA 13 nvcc matches the fast desktop SASS. Do not install it
# on Ada/Ampere — a CUDA 13 binary needs driver 580, which those boxes often
# do not have. Non-Blackwell keeps the image's nvcc (12.x is fine).
if [[ "${XN_NEED_CUDA13}" == "1" ]]; then
  # shellcheck disable=SC1091
  source "${ROOT}/scripts/ensure-cuda13.sh" || true
  if command -v nvcc >/dev/null 2>&1 && ! nvcc --version 2>/dev/null | grep -q 'release 13'; then
    echo "WARNING: Blackwell GPU but nvcc is not CUDA 13. H/s will be lower. Continuing." >&2
  fi
fi

if ! command -v nvcc >/dev/null 2>&1; then
  echo "nvcc not found. Install the NVIDIA CUDA Toolkit and put it on PATH." >&2
  echo "Use a CUDA devel image, not a runtime-only image." >&2
  exit 1
fi

NVCC_VER="$(nvcc --version 2>/dev/null | tr '\n' ' ' || true)"
echo "nvcc: ${NVCC_VER}"

nvcc_has_blackwell() {
  nvcc --version 2>/dev/null | grep -qE 'release 12\.(8|9)|release 13'
}

ARCH="${XN_BUILD_ARCH}"
if [[ "${ARCH}" == "75;86;89;90;120" ]] && ! nvcc_has_blackwell; then
  ARCH="75;86;89;90"
  echo "nvcc is older than 12.8 — fat cubin without sm_120"
fi
if [[ "${ARCH}" == *"120"* || "${ARCH}" == *"100"* ]] && ! nvcc_has_blackwell; then
  echo "ERROR: this GPU needs nvcc 12.8+ (CUDA 13 preferred). Have: ${NVCC_VER}" >&2
  echo "Install a CUDA devel image with 12.8/13, or run scripts/ensure-cuda13.sh" >&2
  exit 1
fi
echo "Building xnminer CMAKE_CUDA_ARCHITECTURES=${ARCH} compiler=${CMAKE_CUDA_COMPILER:-nvcc} gpu=${XN_GPU_NAME:-unknown} vram=${XN_GPU_VRAM_MIB}MiB cpu=${XN_CPU_CORES} auto_lanes=${XN_SUGGESTED_LANES}"

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

rm -f "${BUILD}/CMakeCache.txt"
cmake -S "${ROOT}" -B "${BUILD}" "${GEN[@]}" "${CMAKE_ARGS[@]}"
cmake --build "${BUILD}" --parallel

echo
if command -v cuobjdump >/dev/null 2>&1 && [[ -x "${BUILD}/bin/xnminer" ]]; then
  echo "Embedded GPU ELF (this box: sm_${XN_GPU_ARCH:-unknown}):"
  cuobjdump -lelf "${BUILD}/bin/xnminer" || true
fi
echo "OK: ${BUILD}/bin/xnminer"
mkdir -p "${ROOT}/data"
{
  echo "arch=${ARCH}"
  echo "gpu=${XN_GPU_NAME:-unknown}"
  echo "sm=${XN_GPU_ARCH:-}"
  echo "family=${XN_GPU_FAMILY:-}"
  echo "vram_mib=${XN_GPU_VRAM_MIB}"
  echo "cpu_cores=${XN_CPU_CORES}"
  echo "auto_lanes=${XN_SUGGESTED_LANES}"
  echo "auto_batch_m100=${XN_SUGGESTED_BATCH}"
  echo "auto_keygen=${XN_SUGGESTED_KEYGEN}"
  echo "device=${XN_DEVICE}"
} > "${ROOT}/data/build_hw"
nvcc --version > "${ROOT}/data/nvcc_version" 2>/dev/null || true
if command -v git >/dev/null 2>&1 && git -C "${ROOT}" rev-parse HEAD >/dev/null 2>&1; then
  git -C "${ROOT}" rev-parse HEAD > "${ROOT}/data/build_sha"
fi
