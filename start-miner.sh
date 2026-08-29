#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${ROOT}"

BIN="${ROOT}/build/bin/xnminer"
if [[ ! -x "${BIN}" ]]; then
  echo "Building pure CUDA miner (arch / lanes / batch from this GPU and CPU)..."
  "${ROOT}/build.sh"
else
  # shellcheck disable=SC1091
  source "${ROOT}/scripts/detect-hardware.sh" 2>/dev/null || true
  if [[ -n "${XN_GPU_NAME:-}" ]]; then
    echo "This box: ${XN_GPU_NAME} sm_${XN_GPU_ARCH} ${XN_GPU_VRAM_MIB} MiB, ${XN_CPU_CORES} CPU cores → ${XN_SUGGESTED_LANES} lanes x ~${XN_SUGGESTED_BATCH} @ m=100"
  fi
fi

exec "${BIN}" "$@"
